#pragma once

#include <cassert>
#include <string>
#include <utility>

#include "fmt/format.h"

class SourceManager;

class SourceLocation {
 public:
  uint32_t getOffset() const { return this->offset; }
 private:
  friend class SourceManager;
  explicit SourceLocation(uint32_t offset) : offset(offset) { }

  uint32_t offset;
};

class SourceRange {
 public:
  SourceRange(SourceLocation begin, SourceLocation end) : begin(begin), end(end) { }

  SourceLocation getBegin() const { return this->begin; }
  SourceLocation getEnd() const { return this->end; }

  std::string toReadableString() const { return ""; }

 private:
  SourceLocation begin;
  SourceLocation end;
};
