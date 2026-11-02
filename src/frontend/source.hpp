#pragma once

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source-location.hpp"

class Source {
 public:
  Source(std::string filename, std::string text)
      : filename(std::move(filename)), text(std::move(text)) { }

  std::string_view getFilename() const { return this->filename; }
  size_t getSize() const { return this->text.size(); }
  std::string_view getText() const { return this->text; }

 private:
  std::string filename;
  std::string text;
};

// Opaque handle to a file registered in a SourceManager.
class FileID {
 private:
  friend class SourceManager;
  explicit FileID(uint32_t index) : index(index) {}
  uint32_t index;
};

class SourceManager {
 public:
  SourceManager() = default;
  SourceManager(const SourceManager &) = delete;
  SourceManager &operator=(const SourceManager &) = delete;

  FileID addSource(std::string filename, std::string contents);

  SourceLocation getLocation(FileID file_id, uint32_t local_pos) const;
  SourceRange getRange(FileID file_id, uint32_t local_begin, uint32_t local_end) const;

  struct ResolvedLocation {
    FileID file;
    uint32_t local_offset;
    uint32_t line;
    uint32_t column;
  };

  std::string_view getText(FileID file_id) const;
  uint32_t getLineStart(FileID id, uint32_t line);
  const Source &getSource(FileID id) const;
  size_t findEntryIndex(SourceLocation loc) const;
  FileID getFileID(SourceLocation loc) const;
  uint32_t getLocalOffset(SourceLocation loc) const;
  ResolvedLocation resolve(SourceLocation loc) const;

 private:
  struct Entry {
    uint32_t base;  // base + local_offset = global_offset
    std::unique_ptr<Source> source;
    std::vector<uint32_t> line_starts;

    Entry(uint32_t base, std::unique_ptr<Source> source, std::vector<uint32_t> line_starts)
        : base(base), source(std::move(source)), line_starts(std::move(line_starts)) { }
  };

  std::vector<Entry> entries;
  uint32_t next_offset = 0;
};
