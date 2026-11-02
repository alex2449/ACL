#pragma once

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "source.hpp"
#include "source-location.hpp"
#include "../util/color.hpp"

enum class DiagKind {
  Error,
  Warning,
  Note,
  Fatal
};

struct DiagSourceInfo {
  SourceLocation caret;
  std::vector<SourceRange> underlines;

  DiagSourceInfo(SourceLocation caret) : caret(caret) { }
  DiagSourceInfo(SourceRange range) : caret(range.getBegin()), underlines{range} { }
  DiagSourceInfo(SourceLocation caret, std::vector<SourceRange> underlines)
      : caret(caret), underlines(std::move(underlines)) { }
};

class Diagnostics {
 public:
  explicit Diagnostics(SourceManager *src_mger, bool with_color) : src_mger(src_mger), with_color(with_color) { }

  template<typename... Args>
  void error(DiagSourceInfo loc, fmt::format_string<Args...> fmt, Args&&... args) {
    this->number_of_errors++;
    this->report(DiagKind::Error, loc, fmt::format(fmt, std::forward<Args>(args)...));
  }

  template<typename... Args>
  void error(fmt::format_string<Args...> fmt, Args&&... args) {
    this->number_of_errors++;
    this->report(DiagKind::Error, fmt::format(fmt, std::forward<Args>(args)...));
  }

  template<typename... Args>
  void warning(DiagSourceInfo loc, fmt::format_string<Args...> fmt, Args&&... args) {
    this->report(DiagKind::Warning, loc, fmt::format(fmt, std::forward<Args>(args)...));
  }

  template<typename... Args>
  void warning(fmt::format_string<Args...> fmt, Args&&... args) {
    this->report(DiagKind::Warning, fmt::format(fmt, std::forward<Args>(args)...));
  }

  template<typename... Args>
  void note(DiagSourceInfo loc, fmt::format_string<Args...> fmt, Args&&... args) {
    this->report(DiagKind::Note, loc, fmt::format(fmt, std::forward<Args>(args)...));
  }

  template<typename... Args>
  void note(fmt::format_string<Args...> fmt, Args&&... args) {
    this->report(DiagKind::Note, fmt::format(fmt, std::forward<Args>(args)...));
  }

  template<typename... Args>
  [[noreturn]] void fatal(fmt::format_string<Args...> fmt, Args&&... args) {
    this->report(DiagKind::Fatal, fmt::format(fmt, std::forward<Args>(args)...));
    std::exit(1);
  }

  template<typename... Args>
  [[noreturn]] void fatal(DiagSourceInfo loc, fmt::format_string<Args...> fmt, Args&&... args) {
    this->report(DiagKind::Fatal, loc, fmt::format(fmt, std::forward<Args>(args)...));
    std::exit(1);
  }

  bool hasErrors() const { return this->number_of_errors > 0; }
  void printSummary() const;

 private:
  void report(DiagKind kind, const DiagSourceInfo &src_info, std::string_view message);
  void report(DiagKind kind, std::string_view message);

  void printMessage(DiagKind kind, std::optional<SourceLocation> caret, std::string_view message) const;
  void printSource(SourceLocation caret, const std::vector<SourceRange> &underlines) const;

  static std::string_view diagKindToString(DiagKind kind);
  static std::string_view diagKindToColor(DiagKind kind);

  ColorScope withColor(std::string_view color) const {
    return ColorScope(std::cerr, this->with_color, color);
  }

  SourceManager *src_mger;
  bool with_color;
  int number_of_errors = 0;
  static const int max_number_of_errors = 20;
};
