#include "type-provider.hpp"

IntegerType TypeProvider::int8_ = IntegerType(true, 8);
IntegerType TypeProvider::int16_ = IntegerType(true, 16);
IntegerType TypeProvider::int32_ = IntegerType(true, 32);
IntegerType TypeProvider::int64_ = IntegerType(true, 64);
IntegerType TypeProvider::uint8_ = IntegerType(false, 8);
IntegerType TypeProvider::uint16_ = IntegerType(false, 16);
IntegerType TypeProvider::uint32_ = IntegerType(false, 32);
IntegerType TypeProvider::uint64_ = IntegerType(false, 64);
BoolType TypeProvider::bool_ = BoolType();
CharType TypeProvider::char_ = CharType();
StringType TypeProvider::string_ = StringType();
StrType TypeProvider::str_ = StrType();

std::unordered_map<::Enum *, std::unique_ptr<EnumType>> TypeProvider::enum_types;
std::unordered_map<PointerKey, std::unique_ptr<PointerType>, PointerKeyHash> TypeProvider::pointer_types;
std::unordered_map<ArrayKey, std::unique_ptr<ArrayType>, ArrayKeyHash> TypeProvider::array_types;
std::unordered_map<const Type *, std::unique_ptr<SliceType>> TypeProvider::slice_types;
std::unordered_map<::Struct *, std::unique_ptr<StructType>> TypeProvider::struct_types;
std::unordered_map<::Variant *, std::unique_ptr<VariantType>> TypeProvider::variant_types;

const IntegerType *TypeProvider::Int(bool is_signed, unsigned bits) {
  switch (bits) {
  case 8:  return is_signed ? TypeProvider::Int8()  : TypeProvider::UInt8();
  case 16: return is_signed ? TypeProvider::Int16() : TypeProvider::UInt16();
  case 32: return is_signed ? TypeProvider::Int32() : TypeProvider::UInt32();
  case 64: return is_signed ? TypeProvider::Int64() : TypeProvider::UInt64();
  default:
    assert(false);
  }
}

const EnumType *TypeProvider::Enum(::Enum *enum_) {
  auto it = TypeProvider::enum_types.find(enum_);

  if (it != TypeProvider::enum_types.end())
    return it->second.get();

  std::unique_ptr<EnumType> type = std::unique_ptr<EnumType>(new EnumType(enum_));
  const EnumType *raw = type.get();
  TypeProvider::enum_types[enum_] = std::move(type);
  return raw;
}

const PointerType *TypeProvider::Pointer(const Type *pointee, bool is_owning) {
  PointerKey key{pointee, is_owning};

  auto it = TypeProvider::pointer_types.find(key);

  if (it != TypeProvider::pointer_types.end())
    return it->second.get();

  std::unique_ptr<PointerType> type = std::unique_ptr<PointerType>(new PointerType(pointee, is_owning));
  const PointerType *raw = type.get();
  TypeProvider::pointer_types[key] = std::move(type);
  return raw;
}

const ArrayType *TypeProvider::Array(const Type *element) {
  ArrayKey key{element, {}};

  auto it = TypeProvider::array_types.find(key);

  if (it != TypeProvider::array_types.end())
    return it->second.get();

  std::unique_ptr<ArrayType> type = std::unique_ptr<ArrayType>(new ArrayType(element));
  const ArrayType *raw = type.get();
  TypeProvider::array_types.emplace(key, std::move(type));
  return raw;
}

const ArrayType *TypeProvider::Array(const Type *element, uint64_t length) {
  assert(length > 0);
  ArrayKey key{element, length};

  auto it = TypeProvider::array_types.find(key);

  if (it != TypeProvider::array_types.end())
    return it->second.get();

  std::unique_ptr<ArrayType> type = std::unique_ptr<ArrayType>(new ArrayType(element, length));
  const ArrayType *raw = type.get();
  TypeProvider::array_types.emplace(key, std::move(type));
  return raw;
}

const SliceType *TypeProvider::Slice(const Type *element) {
  auto it = TypeProvider::slice_types.find(element);

  if (it != TypeProvider::slice_types.end())
    return it->second.get();

  std::unique_ptr<SliceType> type = std::unique_ptr<SliceType>(new SliceType(element));
  const SliceType *raw = type.get();
  TypeProvider::slice_types[element] = std::move(type);
  return raw;
}

const StructType *TypeProvider::Struct(::Struct *struct_) {
  auto it = TypeProvider::struct_types.find(struct_);

  if (it != TypeProvider::struct_types.end())
    return it->second.get();

  std::unique_ptr<StructType> type = std::unique_ptr<StructType>(new StructType(struct_));
  const StructType *raw = type.get();
  TypeProvider::struct_types[struct_] = std::move(type);
  return raw;
}

const VariantType *TypeProvider::Variant(::Variant *variant) {
  auto it = TypeProvider::variant_types.find(variant);

  if (it != TypeProvider::variant_types.end())
    return it->second.get();

  std::unique_ptr<VariantType> type = std::unique_ptr<VariantType>(new VariantType(variant));
  const VariantType *raw = type.get();
  TypeProvider::variant_types[variant] = std::move(type);
  return raw;
}
