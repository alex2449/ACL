#include "util.hpp"

#include <array>
#include <cassert>
#include <string>

bool isAsciiPrintable(char c) {
  return 0x20 <= c && c <= 0x7E;
}

static char intToHexDigit(int c) {
  if (0 <= c && c <= 9)
    return c + '0';
  if (10 <= c && c <= 15)
    return c - 10 + 'A';
  assert(false && "c is larger than a hex digit");
}

static std::string charValueToLiteralWithoutQuotes(char c) {
  if (isAsciiPrintable(c)) {
    if (c == '\'' || c == '\"' || c == '\\')
      return {'\\', c};
    return {c};
  }

  char high = intToHexDigit((c >> 4) & 0x0F);
  char low = intToHexDigit(c & 0x0F);
  return {'\\', 'x', high, low};
}

std::string charValueToLiteral(char c) {
  return "\'" + charValueToLiteralWithoutQuotes(c) + "\'";
}

std::string stringValueToLiteralWithoutQuotes(const std::string &s) {
  std::string buffer;
  for (char c : s)
    buffer += charValueToLiteralWithoutQuotes(c);
  return buffer;
}

std::string stringValueToLiteral(const std::string &s) {
  std::string buffer = "\"";
  for (char c : s)
    buffer += charValueToLiteralWithoutQuotes(c);
  return buffer + "\"";
}

std::string ordinal(int n) {
  assert(n >= 0);
  // https://stackoverflow.com/questions/9647202/ordinal-numbers-replacement
  if (11 <= n % 100 && n % 100 <= 13)
    return std::to_string(n) + "th";
  std::array<const char *, 5> suffixes = {"th", "st", "nd", "rd", "th"};
  return std::to_string(n) + suffixes[std::min(n % 10, 4)];
}
