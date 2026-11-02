#include <unistd.h>
#include <sys/wait.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include "fmt/format.h"
#include "./frontend/acl.hpp"
#include "./frontend/ast-dumper.hpp"
#include "./frontend/diagnostics.hpp"
#include "./frontend/lexer.hpp"
#include "./frontend/parser.hpp"
#include "./frontend/sema.hpp"
#include "./frontend/source.hpp"
#include "./CFG/cfg.hpp"
#include "./CFG/cfg-builder.hpp"
#include "./CFG/cfg-printer.hpp"
// #include "definite-assignment.hpp"
#include "./LLC/llc.hpp"
#include "./LLC/llc-generator.hpp"
#include "./LLC/llc-printer.hpp"
#include "./util/cxxopts.hpp"

struct Options {
  std::optional<std::string> filename;
  bool dump_ast = false;
  bool dump_cfg = false;
  bool emit_c = false;
};

static Options parseCommandLineArguments(int argc, char *argv[], Diagnostics *diag) {
  Options options;

  cxxopts::Options cli("aclc", "Compiler for the ACL programming language");

  cli.add_options()
    ("dump-ast", "Print the AST to stdout", cxxopts::value<bool>(options.dump_ast))
    ("dump-cfg", "Print the CFG to stdout", cxxopts::value<bool>(options.dump_cfg))
    ("emit-c", "Emit C source file")
    ("h,help", "Show usage information")
    ("filename", "Input source file", cxxopts::value<std::string>());

  cli.parse_positional({"filename"});
  cli.positional_help("FILENAME");

  cxxopts::ParseResult result;

  try {
    result = cli.parse(argc, argv);
  } catch (const cxxopts::exceptions::exception &e) {
    diag->fatal("{}", e.what());
  }

  if (result.count("help")) {
    std::cout << cli.help();
    std::exit(0);
  }

  if (result.count("filename")) {
    options.filename = result["filename"].as<std::string>();
  } else {
    diag->fatal("no input file specified");
  }

  options.emit_c = result.count("emit-c") != 0;
  return options;
}

static std::string readFileContents(const std::string &filename, Diagnostics *diag) {
  std::ifstream file;
  file.open(filename);

  // We check only if the file exists and not whether it is an ordinary file
  // whose format we can read. For example, this will not fail if the file is
  // a directory. The file will be read anyway and it will fail at lexing/parsing
  // with some weird error message.
  if (file.fail())
    diag->fatal("no such file: '{}'", filename);

  std::stringstream buffer;
  buffer << file.rdbuf();
  file.close();

  return buffer.str();
}

std::string canonicalizePath(const std::string &path, Diagnostics *diag) {
  std::error_code ec;
  std::filesystem::path canonical = std::filesystem::canonical(path, ec);

  if (ec == std::errc::no_such_file_or_directory)
    diag->fatal("file '{}' not found", path);

  if (ec)
    diag->fatal("cannot open '{}': {}", path, ec.message());

  if (!std::filesystem::is_regular_file(canonical))
    diag->fatal("'{}' is not a regular file", path);

  return canonical.string();
}

std::string resolveImportPath(const std::string &importing_file_canonical_path, const std::string &import_string, Diagnostics *diag) {
  std::filesystem::path importing_dir = std::filesystem::path(importing_file_canonical_path).parent_path();
  std::filesystem::path joined = importing_dir / import_string;
  return canonicalizePath(joined.string(), diag);
}

SourceUnit *getOrCreate(const std::string &canonical,
                        std::unordered_map<std::string, SourceUnit *> *loaded,
                        std::vector<SourceUnit *> *worklist,
                        Program *program,
                        SourceManager *src_mger,
                        Diagnostics *diag) {
  std::unordered_map<std::string, SourceUnit *>::const_iterator it = loaded->find(canonical);

  if (it != loaded->end())
    return it->second;

  std::string text = readFileContents(canonical, diag);
  FileID file = src_mger->addSource(canonical, std::move(text));

  Lexer lexer(file, src_mger, diag);
  Parser parser(&lexer, diag);

  std::unique_ptr<SourceUnit> unit = parser.parse(canonical);
  SourceUnit *raw_unit = unit.get();

  loaded->insert({canonical, raw_unit});
  program->units.push_back(std::move(unit));
  worklist->push_back(raw_unit);
  return raw_unit;
}

Program *buildProgram(const std::string &entry_filename, SourceManager *src_mger, Diagnostics *diag) {
  Program *program = new Program();

  // Map a canonical path to its SourceUnit
  std::unordered_map<std::string, SourceUnit *> loaded;
  std::vector<SourceUnit *> worklist;

  getOrCreate(canonicalizePath(entry_filename, diag), &loaded, &worklist, program, src_mger, diag);
  std::string string_unit_path = canonicalizePath("/usr/local/share/acl/lib/string.acl", diag);
  getOrCreate(string_unit_path, &loaded, &worklist, program, src_mger, diag);

  while (!worklist.empty()) {
    SourceUnit *unit = worklist.back();
    worklist.pop_back();

    for (Import &import : unit->imports) {
      std::string import_path = resolveImportPath(unit->path, import.path, diag);
      import.unit = getOrCreate(import_path, &loaded, &worklist, program, src_mger, diag);  // set for every import, new or not
    }
  }

  return program;
}

using CFGMap = std::unordered_map<const Function *, cfg::CFG>;

static CFGMap buildCFGs(const std::vector<Function *> &funcs, std::unordered_map<const Type *, TypeInfo> *type_infos, Diagnostics *diag) {
  CFGMap cfg_map;

  for (Function *func : funcs) {
    if (func->body != nullptr) {
      CFGBuilder builder(func, type_infos, diag);
      cfg::CFG cfg = builder.build();
      cfg_map.insert({func, std::move(cfg)});
    }
  }

  return cfg_map;
}

int main(int argc, char *argv[]) {
  SourceManager src_mger;

  Diagnostics diag(&src_mger, isatty(STDERR_FILENO) == 1);

  Options options = parseCommandLineArguments(argc, argv, &diag);
  Program *program = buildProgram(options.filename.value(), &src_mger, &diag);

  Sema sema(program, &diag);
  SemaResult sema_res = sema.process();

  if (options.dump_ast) {
    ASTDumper dumper(std::cout, isatty(STDOUT_FILENO) == 1);

    for (const std::unique_ptr<SourceUnit> &su : program->units)
      dumper.visit(su.get());
  }

  if (diag.hasErrors()) {
    diag.printSummary();
    return 1;
  }

  // delete program;
  // return 0;

  CFGMap cfg_map = buildCFGs(sema_res.funcs, &sema_res.type_infos, &diag);

  if (options.dump_cfg) {
    CFGPrinter cfg_printer(std::cout, 4);
    cfg_printer.visit(sema_res.funcs, cfg_map);
  }

  if (diag.hasErrors()) {
    diag.printSummary();
    return 1;
  }

  LLCGenerator generator(&sema_res.funcs, &sema_res.type_infos, &cfg_map);
  llc::Module module = generator.generate();

  std::filesystem::path input = options.filename.value();
  std::filesystem::path output = input.filename();
  output.replace_extension(".c");
  std::ofstream out(output);

  if (!out.is_open())
    diag.fatal("failed to open {}", output.string());

  LLCPrinter llc_printer(out, 4);
  llc_printer.visit(&module);
  out.close();

  if (options.emit_c)
    return 0;

  pid_t pid = fork();

  if (pid == -1)
    diag.fatal("failed to fork");

  if (pid == 0) {
    std::filesystem::path runtime_dir = "/usr/local/share/acl/runtime";
    std::string main_c_path = (runtime_dir / "main.c").string();
    std::string runtime_c_path = (runtime_dir / "runtime.c").string();
    std::string include_dir = runtime_dir.string();

    execlp("clang", "clang", "-g", output.string().c_str(),
            main_c_path.c_str(), runtime_c_path.c_str(),
            "-I", include_dir.c_str(),
            "-Wall", "-Wextra", "-Wpedantic", "-w",
            "-o", "a.out",
            (char *)nullptr);
    perror("clang");
    _exit(127);
  }

  int status;

  if (waitpid(pid, &status, 0) == -1)
      diag.fatal("waitpid failed");

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
      return 1;

  std::filesystem::remove(output);

  return 0;
}
