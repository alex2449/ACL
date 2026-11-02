#include "diagnostics.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <utility>

#include "fmt/format.h"
#include "source-location.hpp"
#include "source.hpp"

void Diagnostics::report(DiagKind kind, const DiagSourceInfo &src_info, std::string_view message) {
  this->printMessage(kind, src_info.caret, message);
  this->printSource(src_info.caret, src_info.underlines);
}

void Diagnostics::report(DiagKind kind, std::string_view message) {
  this->printMessage(kind, std::nullopt, message);
}

void Diagnostics::printMessage(DiagKind kind, std::optional<SourceLocation> caret, std::string_view message) const {
  if (caret.has_value()) {
    SourceManager::ResolvedLocation resolved = this->src_mger->resolve(*caret);
    std::string_view filename = this->src_mger->getSource(resolved.file).getFilename();
    this->withColor(BOLD_HIGH_INTENSITY_WHITE) << fmt::format("{}:{}:{}: ", filename, resolved.line, resolved.column);
  }

  this->withColor(diagKindToColor(kind)) << diagKindToString(kind) << ": ";
  this->withColor(BOLD_HIGH_INTENSITY_WHITE) << message << "\n";
}

void Diagnostics::printSource(SourceLocation caret, const std::vector<SourceRange> &underlines) const {
  SourceManager::ResolvedLocation resolved = this->src_mger->resolve(caret);

  uint32_t line_start = this->src_mger->getLineStart(resolved.file, resolved.line);
  std::string_view text = this->src_mger->getText(resolved.file);

  // End of the line: the position of '\n', or the end of the file.
  uint32_t line_end = line_start;
  while (line_end < text.size() && text[line_end] != '\n')
    ++line_end;

  // Print source.
  std::cerr << text.substr(line_start, line_end - line_start) << "\n";

  // Convert the ranges to file-local offsets (end is inclusive), and check whether
  // they are all within the caret's line.
  struct LocalRange { uint32_t begin, end; };
  std::vector<LocalRange> ranges;
  bool spans_multiple_lines = false;

  for (const SourceRange &range : underlines) {
    LocalRange local{this->src_mger->getLocalOffset(range.getBegin()),
                     this->src_mger->getLocalOffset(range.getEnd())};

    if (local.begin < line_start || local.end > line_end)
      spans_multiple_lines = true;

    ranges.push_back(local);
  }

  // Print the caret character '^' and underline the source ranges.
  // line_end is included so the caret can be printed at the end of the line or at EOF.
  for (uint32_t offset = line_start, column = 1; offset <= line_end; ++offset) {
    bool is_highlight_position = std::any_of(ranges.begin(), ranges.end(), [offset](const LocalRange &r) {
      return r.begin <= offset && offset <= r.end;
    });

    if (offset < line_end && text[offset] == '\t') {
      // Round to next greater multiple of 8 (and add one).
      uint32_t tab_stop_column = ((column + 7) & ~7u) + 1;
      // Number of characters needed to reach the next tab stop.
      uint32_t n = tab_stop_column - column;

      if (offset == resolved.local_offset) {
        this->withColor(BOLD_GREEN) << '^' << std::string(n - 1, '~');
      } else if (is_highlight_position && !spans_multiple_lines) {
        this->withColor(BOLD_GREEN) << std::string(n, '~');
      } else {
        // Just print the tab character itself.
        std::cerr << '\t';
      }

      column = tab_stop_column;
    } else {
      if (offset == resolved.local_offset) {
        this->withColor(BOLD_GREEN) << '^';
      } else if (is_highlight_position && !spans_multiple_lines) {
        this->withColor(BOLD_GREEN) << '~';
      } else {
        std::cerr << ' ';
      }

      ++column;
    }
  }

  std::cerr << "\n";
}

void Diagnostics::printSummary() const {
  if (this->number_of_errors == 0)
    return;

  std::cerr << fmt::format("{} error{} generated.\n", this->number_of_errors, this->number_of_errors > 1 ? "s" : "");
}

std::string_view Diagnostics::diagKindToString(DiagKind kind) {
  switch (kind) {
  case DiagKind::Error:   return "error";
  case DiagKind::Warning: return "warning";
  case DiagKind::Note:    return "note";
  case DiagKind::Fatal:   return "fatal error";
  }

  assert(false);
}

std::string_view Diagnostics::diagKindToColor(DiagKind kind) {
  switch (kind) {
  case DiagKind::Error:   return BOLD_RED;
  case DiagKind::Warning: return BOLD_PURPLE;
  case DiagKind::Note:    return BOLD_CYAN;
  case DiagKind::Fatal:   return BOLD_RED;
  }

  assert(false);
}
