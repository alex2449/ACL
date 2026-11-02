#include "source.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

FileID SourceManager::addSource(std::string filename, std::string text) {
  uint64_t size = text.size();
  uint64_t end = uint64_t(this->next_offset) + size + 1;

  if (end > UINT32_MAX)
    throw std::length_error("total source size exceeds the 32-bit offset space");

  std::vector<uint32_t> line_starts{0};
  for (uint32_t i = 0; i < size; ++i)
    if (text[i] == '\n')
      line_starts.push_back(i + 1);

  Entry entry(this->next_offset, std::make_unique<Source>(std::move(filename), std::move(text)), std::move(line_starts));
  this->entries.push_back(std::move(entry));

  this->next_offset = static_cast<uint32_t>(end);
  return FileID(this->entries.size() - 1);
}

SourceLocation SourceManager::getLocation(FileID file_id, uint32_t local_pos) const {
  const Entry &entry = this->entries[file_id.index];
  return SourceLocation(entry.base + local_pos);
}

SourceRange SourceManager::getRange(FileID file_id, uint32_t local_begin, uint32_t local_end) const {
  SourceLocation begin = this->getLocation(file_id, local_begin);
  SourceLocation end = this->getLocation(file_id, local_end);
  return SourceRange(begin, end);
}

std::string_view SourceManager::getText(FileID file_id) const {
  return this->entries[file_id.index].source->getText();
}

uint32_t SourceManager::getLineStart(FileID id, uint32_t line) {
  return this->entries.at(id.index).line_starts.at(line - 1);
}

const Source &SourceManager::getSource(FileID id) const {
  return *this->entries.at(id.index).source;
}

SourceManager::ResolvedLocation SourceManager::resolve(SourceLocation loc) const {
  size_t index = this->findEntryIndex(loc);
  const Entry &entry = this->entries[index];
  uint32_t local_offset = loc.getOffset() - entry.base;

  // line_starts is sorted and line_starts[0] == 0, so the line containing local_offset
  // is the last start <= local_offset: one before the first start greater than it.
  auto it = std::upper_bound(entry.line_starts.begin(), entry.line_starts.end(), local_offset);
  size_t line_index = (it - entry.line_starts.begin()) - 1;   // never underflows because starts[0] == 0

  return ResolvedLocation{
      FileID(static_cast<uint32_t>(index)),
      local_offset,
      static_cast<uint32_t>(line_index + 1),
      local_offset - entry.line_starts[line_index] + 1,
  };
}

size_t SourceManager::findEntryIndex(SourceLocation loc) const {
  assert(!this->entries.empty());

  for (size_t i = 1, n = this->entries.size(); i < n; ++i) {
    if (this->entries[i].base > loc.offset) {
      assert(loc.offset <= this->entries[i].base + this->entries[i].source->getSize());
      return i - 1;
    }
  }

  assert(loc.offset <= this->entries.back().base + this->entries.back().source->getSize());
  return this->entries.size() - 1;
}

FileID SourceManager::getFileID(SourceLocation loc) const {
  return FileID(static_cast<uint32_t>(this->findEntryIndex(loc)));
}

uint32_t SourceManager::getLocalOffset(SourceLocation loc) const {
  const Entry &entry = this->entries[this->findEntryIndex(loc)];
  return loc.getOffset() - entry.base;
}
