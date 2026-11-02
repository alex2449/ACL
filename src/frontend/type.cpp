#include "type.hpp"

#include "acl.hpp"
#include "type-provider.hpp"
#include "../util/util.hpp"

bool Type::isDynamicArray() const {
  return this->isArrayType() && this->toArrayType()->isDynamic();
}

bool Type::isStaticArray() const {
  return this->isArrayType() && !this->toArrayType()->isDynamic();
}

bool Type::isValue() const {
  switch (this->getTag()) {
  case Tag::IntegerType:
  case Tag::BoolType:
  case Tag::CharType:
  case Tag::EnumType:
  case Tag::PointerType:
    return true;
  case Tag::ArrayType:
  case Tag::SliceType:
  case Tag::StructType:
  case Tag::VariantType:
  case Tag::StringType:
  case Tag::StrType:
    return false;
  }

  assert(false);
}

std::string Type::toString() const {
  switch (this->getTag()) {
  case Tag::IntegerType: {
    std::string s = this->toIntegerType()->isSigned() ? "int" : "uint";
    s += std::to_string(this->toIntegerType()->getBits());
    return s;
  }
  case Tag::BoolType:
    return "bool";
  case Tag::CharType:
    return "char";
  case Tag::EnumType:
    return this->toEnumType()->getEnum()->name;
  case Tag::PointerType: {
    std::string s = this->toPointerType()->isOwning() ? "^" : "*";
    s += this->toPointerType()->getPointee()->toString();
    return s;
  }
  case Tag::ArrayType: {
    std::string s = "[";
    if (!this->toArrayType()->isDynamic())
      s += std::to_string(this->toArrayType()->getLength());
    s += "]";
    s += this->toArrayType()->getElement()->toString();
    return s;
  }
  case Tag::SliceType:
    return "[*]" + this->toSliceType()->getElement()->toString();
  case Tag::StructType:
    return this->toStructType()->getStruct()->name;
  case Tag::VariantType:
    return this->toVariantType()->getVariant()->name;
  case Tag::StringType:
    return "string";
  case Tag::StrType:
    return "str";
  }

  assert(false);
}

std::string Type::toIdentifier() const {
  switch (this->getTag()) {
  case Tag::IntegerType:
  case Tag::BoolType:
  case Tag::CharType:
    return this->toString();
  case Tag::EnumType:
    return this->toEnumType()->getEnum()->name;
  case Tag::PointerType:
    if (this->toPointerType()->isOwning())
      return "owning_ptr_" + this->toPointerType()->getPointee()->toIdentifier();
    else
      return "ptr_" + this->toPointerType()->getPointee()->toIdentifier();
  case Tag::ArrayType: {
    if (this->toArrayType()->isDynamic())
      return "dyn_arr_" + this->toArrayType()->getElement()->toIdentifier();
    else
      return "arr_" + std::to_string(this->toArrayType()->getLength()) + "_" + this->toArrayType()->getElement()->toIdentifier();
  }
  case Tag::SliceType:
    return "slice_" + this->toSliceType()->getElement()->toIdentifier();
  case Tag::StructType:
    return this->toStructType()->getStruct()->name;
  case Tag::VariantType:
    return this->toVariantType()->getVariant()->name;
  case Tag::StringType:
    return "string";
  case Tag::StrType:
    return "str";
  }

  assert(false);
}

std::vector<const Type *> Type::sizeDeps() const {
  switch (this->getTag()) {
  case Tag::StructType: {
    std::vector<const Type *> deps;

    for (const StructField &field : this->toStructType()->getStruct()->fields) {
      assert(field.type != nullptr);

      if (field.type->isStructType() || field.type->isVariantType())
        deps.push_back(field.type);
      else if (field.type->isStaticArray() && (field.type->toArrayType()->getElement()->isStructType()
                                               || field.type->toArrayType()->getElement()->isVariantType()))
        deps.push_back(field.type->toArrayType()->getElement());
    }

    return deps;
  }
  case Tag::VariantType: {
    std::vector<const Type *> deps;

    for (const VariantAlternative &alternative : this->toVariantType()->getVariant()->alternatives) {
      assert(alternative.type != nullptr);

      if (alternative.type->isStructType() || alternative.type->isVariantType())
        deps.push_back(alternative.type);
      else if (alternative.type->isStaticArray() && (alternative.type->toArrayType()->getElement()->isStructType()
                                                     || alternative.type->toArrayType()->getElement()->isVariantType()))
        deps.push_back(alternative.type->toArrayType()->getElement());
    }

    return deps;
  }
  case Tag::IntegerType:
  case Tag::BoolType:
  case Tag::CharType:
  case Tag::EnumType:
  case Tag::PointerType:
  case Tag::ArrayType:
  case Tag::SliceType:
  case Tag::StringType:
  case Tag::StrType:
    assert(false && "type must be user defined");
  }

  assert(false);
}

const Type *Type::indexResult() const {
  switch (this->getTag()) {
  case Tag::ArrayType:
    return this->toArrayType()->getElement();
  case Tag::SliceType:
    return this->toSliceType()->getElement();
  case Tag::StringType:
  case Tag::StrType:
    return TypeProvider::Char();
  case Tag::IntegerType:
  case Tag::BoolType:
  case Tag::CharType:
  case Tag::PointerType:
  case Tag::StructType:
  case Tag::VariantType:
  case Tag::EnumType:
    assert(false && "type can not be indexed");
  }

  assert(false);
}

bool Type::isImplicitlyConvertibleTo(const Type *target) const {
  if (target->isVariantType() && target->toVariantType()->lookupAlternative(this) != nullptr)
    return true;

  switch (this->getTag()) {
  case Tag::IntegerType: {
    if (target->isIntegerType())
      return (this->toIntegerType()->isSigned() == target->toIntegerType()->isSigned()
              && this->toIntegerType()->getBits() <= target->toIntegerType()->getBits());

    return false;
  }
  case Tag::BoolType:
    return target->isBoolType();
  case Tag::CharType:
    return this->isEqualTo(target);
  case Tag::EnumType:
    return this->isEqualTo(target);
  case Tag::PointerType: {
    if (target->isPointerType()) {
      if (!this->toPointerType()->isOwning() && target->toPointerType()->isOwning())
        return false;
      return this->toPointerType()->getPointee()->isEqualTo(target->toPointerType()->getPointee());
    }
    return false;
  }
  case Tag::ArrayType: {
    if (target->isSliceType())
      return this->toArrayType()->getElement()->isEqualTo(target->toSliceType()->getElement());
    return this->isEqualTo(target);
  }
  case Tag::SliceType:
  case Tag::StructType:
  case Tag::VariantType:
    return this->isEqualTo(target);
  case Tag::StringType:
    return target->isStringType() || target->isStrType();
  case Tag::StrType:
    return target->isStrType();
  }

  assert(false);
}

bool Type::isExplicitlyConvertibleTo(const Type *target) const {
  switch (this->getTag()) {
  case Tag::IntegerType: {
    if (target->isIntegerType())
      return (this->toIntegerType()->isSigned() == target->toIntegerType()->isSigned()
              || this->toIntegerType()->getBits() == target->toIntegerType()->getBits());

    if (target->isCharType() || target->isEnumType())
      return !this->toIntegerType()->isSigned() && this->toIntegerType()->getBits() == 8;

    return false;
  }
  case Tag::BoolType:
    return target->isBoolType();
  case Tag::CharType:
  case Tag::EnumType:
    if (target->isIntegerType())
      return !target->toIntegerType()->isSigned() && target->toIntegerType()->getBits() == 8;
    return this->isEqualTo(target);
  case Tag::PointerType:
  case Tag::ArrayType:
  case Tag::SliceType:
  case Tag::StructType:
  case Tag::VariantType:
  case Tag::StringType:
  case Tag::StrType:
    return this->isImplicitlyConvertibleTo(target);
  }

  assert(false);
}

bool Type::isTrivial(std::unordered_map<const Type *, TypeInfo> &type_infos) const {
  return !this->requiresDrop(type_infos) && !this->requiresTake(type_infos);
}

bool Type::requiresDrop(const std::unordered_map<const Type *, TypeInfo> &type_infos) const {
  if (this->getTag() == Tag::PointerType)
    return this->toPointerType()->isOwning();

  const TypeInfo &type_info = type_infos.at(this);

  if (type_info.drop != nullptr)
    return true;

  switch (this->getTag()) {
  case Tag::IntegerType:
  case Tag::BoolType:
  case Tag::CharType:
  case Tag::EnumType:
    return false;
  case Tag::PointerType:
    assert(false && "handled above");
  case Tag::ArrayType:
    return this->toArrayType()->isDynamic() || this->toArrayType()->getElement()->requiresDrop(type_infos);
  case Tag::SliceType:
    return false;
  case Tag::StructType:
    for (const StructField &field : this->toStructType()->getStruct()->fields)
      if (field.type->requiresDrop(type_infos))
        return true;
    return false;
  case Tag::VariantType:
    for (const VariantAlternative &alt : this->toVariantType()->getVariant()->alternatives)
      if (alt.type->requiresDrop(type_infos))
        return true;
    return false;
  case Tag::StringType:
    return true;
  case Tag::StrType:
    return false;
  }

  assert(false);
}

bool Type::requiresTake(const std::unordered_map<const Type *, TypeInfo> &type_infos) const {
  if (this->getTag() == Tag::PointerType)
    return this->toPointerType()->isOwning();

  const TypeInfo &type_info = type_infos.at(this);

  if (type_info.take != nullptr)
    return true;

  switch (this->getTag()) {
  case Tag::IntegerType:
  case Tag::BoolType:
  case Tag::CharType:
  case Tag::EnumType:
    return false;
  case Tag::PointerType:
    assert(false && "handled above");
  case Tag::ArrayType:
    return this->toArrayType()->isDynamic() || this->toArrayType()->getElement()->requiresTake(type_infos);
  case Tag::SliceType:
    return false;
  case Tag::StructType:
    for (const StructField &field : this->toStructType()->getStruct()->fields)
      if (field.type->requiresTake(type_infos))
        return true;
    return false;
  case Tag::VariantType:
    for (const VariantAlternative &alt : this->toVariantType()->getVariant()->alternatives)
      if (alt.type->requiresTake(type_infos))
        return true;
    return false;
  case Tag::StringType:
    return true;
  case Tag::StrType:
    return false;
  }

  assert(false);
}

VariantAlternative *VariantType::lookupAlternative(const Type *type) const {
  for (VariantAlternative &alternative : this->getVariant()->alternatives) {
    assert(alternative.type != nullptr);

    if (alternative.type->isEqualTo(type))
      return &alternative;
  }

  return nullptr;
}
