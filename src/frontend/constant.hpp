#pragma once

#include <string>

#include "gmpxx.h"

class Type;
class IntegerType;

struct EnumValue;

struct IntegerConstant;
struct BoolConstant;
struct CharConstant;
struct StringConstant;
struct EnumConstant;
struct NullConstant;

struct Constant {
  enum class Tag {
    #define X(TypeName) TypeName,
    #include "../defs/constant-tags.def"
    #undef X
  };

  #define X(TypeName)                     \
    bool is##TypeName() const;            \
    TypeName *to##TypeName();             \
    const TypeName *to##TypeName() const;
  #include "../defs/constant-tags.def"
  #undef X

  Constant(Tag tag) : tag(tag) { }
  virtual ~Constant() = default;

  std::string toString() const;

  const Constant *convert(const Type *type, const Type *target) const;

  Tag tag;
};

struct IntegerConstant : public Constant {
 public:
  bool fitsIntegerType(const IntegerType *type) const;
  mpz_class value;
 private:
  IntegerConstant(mpz_class value) : Constant(Tag::IntegerConstant), value(std::move(value)) { }
  friend class ConstantProvider;
};

struct BoolConstant : public Constant {
 public:
  bool value;
 private:
  BoolConstant(bool b) : Constant(Tag::BoolConstant), value(b) { }
  friend class ConstantProvider;
};

struct CharConstant : public Constant {
 public:
  char value;
 private:
  CharConstant(char c) : Constant(Tag::CharConstant), value(c) { }
  friend class ConstantProvider;
};

struct StringConstant : public Constant {
 public:
  std::string value;
 private:
  StringConstant(std::string s) : Constant(Tag::StringConstant), value(std::move(s)) { }
  friend class ConstantProvider;
};

struct EnumConstant : public Constant {
 public:
  EnumValue *value;
 private:
  EnumConstant(EnumValue *value) : Constant(Tag::EnumConstant), value(value) { }
  friend class ConstantProvider;
};

struct NullConstant : public Constant {
 private:
  NullConstant() : Constant(Tag::NullConstant) { }
  friend class ConstantProvider;
};
