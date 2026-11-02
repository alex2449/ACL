#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class IntegerType;
class BoolType;
class CharType;
class EnumType;
class PointerType;
class ArrayType;
class SliceType;
class StructType;
class VariantType;
class StringType;
class StrType;

struct TypeInfo;
struct Struct;
struct StructField;
struct Variant;
struct VariantAlternative;
struct Enum;

class Type {
 public:
  enum class Tag {
    #define X(TypeName) TypeName,
    #include "../defs/type-tags.def"
    #undef X
  };

  #define X(TypeName)                        \
    bool is##TypeName() const;               \
    TypeName *to##TypeName();                \
    const TypeName *to##TypeName() const;
  #include "../defs/type-tags.def"
  #undef X

  Tag getTag() const { return this->tag; }

  bool isDynamicArray() const;
  bool isStaticArray() const;

  bool isValue() const;
  bool isComposite() const { return !this->isValue(); }

  std::string toString() const;
  std::string toIdentifier() const;

  bool isEqualTo(const Type *other) const {
    return this == other;
  }

  // Return all the direct struct and variant dependencies of this type.
  // Must be called on a struct or variant.
  std::vector<const Type *> sizeDeps() const;

  const Type *indexResult() const;

  bool isImplicitlyConvertibleTo(const Type *target) const;
  bool isExplicitlyConvertibleTo(const Type *target) const;

  bool isTrivial(std::unordered_map<const Type *, TypeInfo> &type_infos) const;
  bool requiresDrop(const std::unordered_map<const Type *, TypeInfo> &type_infos) const;
  bool requiresTake(const std::unordered_map<const Type *, TypeInfo> &type_infos) const;

 protected:
  explicit Type(Tag tag) : tag(tag) { }
  virtual ~Type() = default;

 private:
  Tag tag;
};

class IntegerType : public Type {
 public:
  bool isSigned() const { return this->is_signed; }
  unsigned getBits() const { return this->bits; }
 private:
  IntegerType(bool is_signed, unsigned bits)
      : Type(Tag::IntegerType), is_signed(is_signed), bits(bits) {
    assert(bits == 8 || bits == 16 || bits == 32 || bits == 64);
  }

  friend class TypeProvider;

  bool is_signed;
  unsigned bits;
};

class BoolType : public Type {
 private:
  BoolType() : Type(Tag::BoolType) { }
  friend class TypeProvider;
};

class CharType : public Type {
 private:
  CharType() : Type(Tag::CharType) { }
  friend class TypeProvider;
};

class EnumType : public Type {
 public:
  Enum *getEnum() const { return this->enum_; }
 private:
  EnumType(Enum *enum_) : Type(Tag::EnumType), enum_(enum_) { }
  friend class TypeProvider;

  Enum *enum_;
};

class PointerType : public Type {
 public:
  const Type *getPointee() const { return this->pointee; }
  bool isOwning() const { return this->is_owning; }

 private:
  PointerType(const Type *pointee, bool is_owning) : Type(Tag::PointerType), pointee(pointee), is_owning(is_owning) { }
  friend class TypeProvider;

  const Type *pointee;
  bool is_owning;
};

class ArrayType : public Type {
 public:
  const Type *getElement() const { return this->element; }
  bool isDynamic() const { return !this->length.has_value(); }
  uint64_t getLength() const { return this->length.value(); }

 private:
  ArrayType(const Type *element, uint64_t length) : Type(Tag::ArrayType), element(element), length(length) { }
  ArrayType(const Type *element) : Type(Tag::ArrayType), element(element) { }
  friend class TypeProvider;

  const Type *element;
  std::optional<uint64_t> length;
};

class SliceType : public Type {
 public:
  const Type *getElement() const { return this->element; }
 private:
  SliceType(const Type *element) : Type(Tag::SliceType), element(element) { }
  friend class TypeProvider;

  const Type *element;
};

class StructType : public Type {
 public:
  Struct *getStruct() const { return this->struct_; }
 private:
  StructType(Struct *struct_) : Type(Tag::StructType), struct_(struct_) { }
  friend class TypeProvider;

  Struct *struct_;
};

class VariantType : public Type {
 public:
  VariantAlternative *lookupAlternative(const Type *type) const;
  Variant *getVariant() const { return this->variant; }
 private:
  VariantType(Variant *variant) : Type(Tag::VariantType), variant(variant) { }
  friend class TypeProvider;

  Variant *variant;
};

class StringType : public Type {
 private:
  StringType() : Type(Tag::StringType) { }
  friend class TypeProvider;
};

class StrType : public Type {
 private:
  StrType() : Type(Tag::StrType) { }
  friend class TypeProvider;
};
