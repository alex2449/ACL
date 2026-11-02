#include "llc-type-provider.hpp"

#include <cassert>

llc::IntegerType llc::TypeProvider::int8_ = llc::IntegerType(true, 8);
llc::IntegerType llc::TypeProvider::int16_ = llc::IntegerType(true, 16);
llc::IntegerType llc::TypeProvider::int32_ = llc::IntegerType(true, 32);
llc::IntegerType llc::TypeProvider::int64_ = llc::IntegerType(true, 64);
llc::IntegerType llc::TypeProvider::uint8_ = llc::IntegerType(false, 8);
llc::IntegerType llc::TypeProvider::uint16_ = llc::IntegerType(false, 16);
llc::IntegerType llc::TypeProvider::uint32_ = llc::IntegerType(false, 32);
llc::IntegerType llc::TypeProvider::uint64_ = llc::IntegerType(false, 64);
llc::BoolType llc::TypeProvider::bool_ = llc::BoolType();
llc::CharType llc::TypeProvider::char_ = llc::CharType();
llc::PtrType llc::TypeProvider::ptr_ = llc::PtrType();
std::unordered_map<llc::Type *, llc::PointerType *> llc::TypeProvider::pointer_types;
std::unordered_map<llc::ArrayKey, llc::ArrayType *, llc::ArrayKeyHash> llc::TypeProvider::array_types;
std::unordered_map<std::string, llc::StructType *> llc::TypeProvider::struct_types;
std::unordered_map<std::string, llc::UnionType *> llc::TypeProvider::union_types;
std::unordered_map<std::string, llc::EnumType *> llc::TypeProvider::enum_types;

llc::PointerType *llc::TypeProvider::Pointer(llc::Type *pointee) {
  auto it = llc::TypeProvider::pointer_types.find(pointee);

  if (it != llc::TypeProvider::pointer_types.end())
    return it->second;

  llc::PointerType *type = new llc::PointerType(pointee);
  llc::TypeProvider::pointer_types[pointee] = type;
  return type;
}

llc::ArrayType *llc::TypeProvider::Array(llc::Type *element, uint64_t length) {
  assert(length > 0);
  ArrayKey key{element, length};

  auto it = TypeProvider::array_types.find(key);

  if (it != TypeProvider::array_types.end())
    return it->second;

  ArrayType *type = new ArrayType(element, length);
  TypeProvider::array_types.emplace(key, type);
  return type;
}

llc::StructType *llc::TypeProvider::Struct(const std::string &name) {
  auto it = llc::TypeProvider::struct_types.find(name);

  if (it != llc::TypeProvider::struct_types.end())
    return it->second;

  llc::StructType *type = new llc::StructType(name);
  llc::TypeProvider::struct_types[name] = type;
  return type;
}

llc::UnionType *llc::TypeProvider::Union(const std::string &name) {
  auto it = llc::TypeProvider::union_types.find(name);

  if (it != llc::TypeProvider::union_types.end())
    return it->second;

  llc::UnionType *type = new llc::UnionType(name);
  llc::TypeProvider::union_types[name] = type;
  return type;
}

llc::EnumType *llc::TypeProvider::Enum(const std::string &name) {
  auto it = llc::TypeProvider::enum_types.find(name);

  if (it != llc::TypeProvider::enum_types.end())
    return it->second;

  llc::EnumType *type = new llc::EnumType(name);
  llc::TypeProvider::enum_types[name] = type;
  return type;
}
