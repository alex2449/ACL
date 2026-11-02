#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "llc.hpp"

namespace llc {

struct ArrayKey {
  Type *element;
  uint64_t length;

  bool operator==(const ArrayKey &other) const {
    return element == other.element && length == other.length;
  }
};

struct ArrayKeyHash {
  size_t operator()(const ArrayKey &k) const {
    size_t h = std::hash<Type *>{}(k.element);
    h ^= std::hash<uint64_t>{}(k.length) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

class TypeProvider {
 public:
  static IntegerType *Int8() { return &int8_; }
  static IntegerType *Int16() { return &int16_; }
  static IntegerType *Int32() { return &int32_; }
  static IntegerType *Int64() { return &int64_; }
  static IntegerType *UInt8() { return &uint8_; }
  static IntegerType *UInt16() { return &uint16_; }
  static IntegerType *UInt32() { return &uint32_; }
  static IntegerType *UInt64() { return &uint64_; }
  static BoolType *Bool() { return &bool_; }
  static CharType *Char() { return &char_; }
  static PtrType *Ptr() { return &ptr_; }
  static PointerType *Pointer(llc::Type *pointee);
  static ArrayType *Array(llc::Type *element, uint64_t length);
  static StructType *Struct(const std::string &name);
  static UnionType *Union(const std::string &name);
  static EnumType *Enum(const std::string &name);

 private:
  static IntegerType int8_;
  static IntegerType int16_;
  static IntegerType int32_;
  static IntegerType int64_;
  static IntegerType uint8_;
  static IntegerType uint16_;
  static IntegerType uint32_;
  static IntegerType uint64_;
  static BoolType bool_;
  static CharType char_;
  static PtrType ptr_;
  static std::unordered_map<Type *, PointerType *> pointer_types;
  static std::unordered_map<ArrayKey, ArrayType *, ArrayKeyHash> array_types;
  static std::unordered_map<std::string, StructType *> struct_types;
  static std::unordered_map<std::string, UnionType *> union_types;
  static std::unordered_map<std::string, EnumType *> enum_types;
};

}
