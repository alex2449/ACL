#pragma once

#include <memory>
#include <unordered_map>

#include "type.hpp"

struct PointerKey {
  const Type *pointee;
  bool is_owning;

  bool operator==(const PointerKey &other) const {
    return pointee == other.pointee && is_owning == other.is_owning;
  }
};

struct PointerKeyHash {
  size_t operator()(const PointerKey &k) const {
    size_t h = std::hash<const Type *>{}(k.pointee);
    h ^= std::hash<bool>{}(k.is_owning) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

struct ArrayKey {
  const Type *element;
  std::optional<uint64_t> length;

  bool operator==(const ArrayKey &other) const {
    return element == other.element && length == other.length;
  }
};

struct ArrayKeyHash {
  size_t operator()(const ArrayKey &k) const {
    size_t h = std::hash<const Type *>{}(k.element);
    if (k.length.has_value()) {
      h ^= std::hash<uint64_t>{}(k.length.value()) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
  }
};

class TypeProvider {
 public:
  static const IntegerType *Int(bool is_signed, unsigned bits);
  static const IntegerType *Int8() { return &int8_; }
  static const IntegerType *Int16() { return &int16_; }
  static const IntegerType *Int32() { return &int32_; }
  static const IntegerType *Int64() { return &int64_; }
  static const IntegerType *UInt8() { return &uint8_; }
  static const IntegerType *UInt16() { return &uint16_; }
  static const IntegerType *UInt32() { return &uint32_; }
  static const IntegerType *UInt64() { return &uint64_; }
  static const BoolType *Bool() { return &bool_; }
  static const CharType *Char() { return &char_; }
  static const StringType *String() { return &string_; }
  static const StrType *Str() { return &str_; }
  static const EnumType *Enum(Enum *enum_);
  static const PointerType *Pointer(const Type *pointee, bool is_owning);
  static const ArrayType *Array(const Type *element);
  static const ArrayType *Array(const Type *element, uint64_t length);
  static const SliceType *Slice(const Type *element);
  static const StructType *Struct(Struct *struct_);
  static const VariantType *Variant(Variant *variant);

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
  static StringType string_;
  static StrType str_;

  static std::unordered_map<::Enum *, std::unique_ptr<EnumType>> enum_types;
  static std::unordered_map<PointerKey, std::unique_ptr<PointerType>, PointerKeyHash> pointer_types;
  static std::unordered_map<ArrayKey, std::unique_ptr<ArrayType>, ArrayKeyHash> array_types;
  static std::unordered_map<const Type *, std::unique_ptr<SliceType>> slice_types;
  static std::unordered_map<::Struct *, std::unique_ptr<StructType>> struct_types;
  static std::unordered_map<::Variant *, std::unique_ptr<VariantType>> variant_types;
};
