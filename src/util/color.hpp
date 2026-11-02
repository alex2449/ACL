#pragma once

#include <iostream>
#include <string_view>

// Color codes taken from https://gist.github.com/RabaDabaDoba/145049536f815903c79944599c6f952a

#define RESET_ "\033[0m"

#define BOLD_BLACK  "\033[1;30m"
#define BOLD_RED    "\033[1;31m"
#define BOLD_GREEN  "\033[1;32m"
#define BOLD_YELLOW "\033[1;33m"
#define BOLD_BLUE   "\033[1;34m"
#define BOLD_PURPLE "\033[1;35m"
#define BOLD_CYAN   "\033[1;36m"
#define BOLD_WHITE  "\033[1;37m"

#define BOLD_HIGH_INTENSITY_BLACK  "\033[1;90m"
#define BOLD_HIGH_INTENSITY_RED    "\033[1;91m"
#define BOLD_HIGH_INTENSITY_GREEN  "\033[1;92m"
#define BOLD_HIGH_INTENSITY_YELLOW "\033[1;93m"
#define BOLD_HIGH_INTENSITY_BLUE   "\033[1;94m"
#define BOLD_HIGH_INTENSITY_PURPLE "\033[1;95m"
#define BOLD_HIGH_INTENSITY_CYAN   "\033[1;96m"
#define BOLD_HIGH_INTENSITY_WHITE  "\033[1;97m"

class ColorScope {
 public:
  ColorScope(std::ostream &os, bool with_color, std::string_view color)
      : os(os), with_color(with_color) {
    if (with_color)
      os << color;
  }

  ~ColorScope() {
    if (this->with_color)
      this->os << RESET_;
  }

  template <typename T>
  std::ostream &operator<<(const T &t) {
    return this->os << t;
  }

 private:
  std::ostream &os;
  bool with_color;
};
