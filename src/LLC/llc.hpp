#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "gmpxx.h"
#include "../util/util.hpp"

class Type;

namespace llc {

struct Module;

struct Function;
struct Parameter;
struct Variable;
struct GlobalStringConstant;
struct BasicBlock;

struct Type;
struct IntegerType;
struct BoolType;
struct CharType;
struct PtrType;
struct PointerType;
struct ArrayType;
struct StructType;
struct UnionType;
struct EnumType;

struct Instruction;
struct StoreInst;
struct VoidCallInst;
struct VarDeclInst;

struct ValueInst;
struct UnaryOpInst;
struct AddressOfInst;
struct LoadInst;
struct BinaryOpInst;
struct CallInst;
struct CastInst;

struct Terminator;
struct GotoTerm;
struct IfTerm;
struct ReturnTerm;
struct SwitchTerm;
struct CallTerm;

struct PlaceBase;
struct Projection;
struct Place;

struct Value;
struct RegisterValue;
struct IntConstantValue;
struct BoolConstantValue;
struct CharConstantValue;
struct EnumConstantValue;
struct NullValue;
struct SizeofValue;
struct GlobalStringValue;

struct Module {
  // Includes types that require a definition (structs, unions, enums)
  std::vector<Type *> types;
  std::vector<Function *> functions;
  std::vector<GlobalStringConstant *> string_constants;

  Module(std::vector<Type *> types,
         std::vector<Function *> functions,
         std::vector<GlobalStringConstant *> string_constants)
      : types(std::move(types)),
        functions(std::move(functions)),
        string_constants(std::move(string_constants)) { }
};

struct Function {
  std::string name;
  std::vector<Parameter *> params;
  Type *return_type;
  bool is_extern;
  std::vector<BasicBlock *> blocks;

  Function(std::string name,
           std::vector<Parameter *> params,
           Type *return_type,
           bool is_extern)
      : name(std::move(name)), params(std::move(params)), return_type(return_type), is_extern(is_extern) { }
};

struct Parameter {
  std::string name;
  llc::Type *type;
  Parameter(std::string name, Type *type) : name(std::move(name)), type(type) { }
};

struct Variable {
  std::string name;
  const Type *type;
  Variable(std::string name, const Type *type) : name(std::move(name)), type(type) { }
  RegisterValue *asValue();
};

struct GlobalStringConstant {
  llc::Variable *global;
  std::string value;

  GlobalStringConstant(Variable *global, std::string value)
      : global(global), value(std::move(value)) { }
};

struct BasicBlock {
  unsigned id;
  std::vector<Instruction *> insts;
  Terminator *terminator = nullptr;

  BasicBlock(unsigned id) : id(id) { }

  void setTerminator(Terminator *term) {
    assert(this->terminator == nullptr);
    this->terminator = term;
  }
};

// Types

struct Type {
  enum class Tag {
    #define X(TypeName) TypeName,
    #include "../defs/llc-type-tags.def"
    #undef X
  };

  Tag tag;

  Type(Tag tag) : tag(tag) { }
  virtual ~Type() = default;

  #define X(TypeName)                     \
    bool is##TypeName() const;            \
    TypeName *to##TypeName();             \
    const TypeName *to##TypeName() const;
  #include "../defs/llc-type-tags.def"
  #undef X

  std::vector<Type *> dependencies() const;
};

struct IntegerType : public Type {
  bool is_signed;
  unsigned bits;
 private:
  IntegerType(bool is_signed, unsigned bits)
      : Type(Tag::IntegerType), is_signed(is_signed), bits(bits) { }
  friend class TypeProvider;
};

struct BoolType : public Type {
 private:
  BoolType() : Type(Tag::BoolType) { }
  friend class TypeProvider;
};

struct CharType : public Type {
 private:
  CharType() : Type(Tag::CharType) { }
  friend class TypeProvider;
};

struct PtrType : public Type {
 private:
  PtrType() : Type(Tag::PtrType) { }
  friend class TypeProvider;
};

struct PointerType : public Type {
  Type *pointee;
 private:
  PointerType(Type *pointee) : Type(Tag::PointerType), pointee(pointee) { }
  friend class TypeProvider;
};

struct ArrayType : public Type {
  Type *element;
  uint64_t length;
 private:
  ArrayType(Type *element, uint64_t length) : Type(Tag::ArrayType), element(element), length(length) { }
  friend class TypeProvider;
};

struct Field {
  std::string name;
  Type *type;
  Field(std::string name, Type *type) : name(std::move(name)), type(type) { }
};

struct StructType : public Type {
  std::string name;
  std::vector<Field *> fields;  // TODO: Remove pointer

  void addField(llc::Field *field) { this->fields.push_back(field); }
  void addFields(std::initializer_list<llc::Field *> fields) {
    this->fields.insert(this->fields.end(), fields);
  }

 private:
  StructType(std::string name) : Type(Tag::StructType), name(std::move(name)) { }
  friend class TypeProvider;
};

struct UnionType : public Type {
  std::string name;
  std::vector<Field *> fields;  // TODO: Remove pointer

  void addField(llc::Field *field) { this->fields.push_back(field); }
  void addFields(std::initializer_list<llc::Field *> fields) {
    this->fields.insert(this->fields.end(), fields);
  }

 private:
  UnionType(std::string name) : Type(Tag::UnionType), name(std::move(name)) { }
  friend class TypeProvider;
};

struct EnumValue {
  std::string name;
  EnumValue(std::string name) : name(std::move(name)) { }
};

struct EnumType : public Type {
  std::string name;
  std::vector<EnumValue *> values;  // TODO: Remove pointer
 private:
  EnumType(std::string name) : Type(Tag::EnumType), name(std::move(name)) { }
  friend class TypeProvider;
};

// Call

struct Call {
  Function *func;
  std::vector<Value *> args;
  Call(Function *func, std::vector<Value *> args) : func(func), args(std::move(args)) { }
};

// Instructions

struct Instruction {
  enum class Tag {
    #define X(TypeName) TypeName,
    #include "../defs/llc-instruction-tags.def"
    #undef X
  };

  Tag tag;

  Instruction(Tag tag) : tag(tag) { }
  virtual ~Instruction() = default;

  #define X(TypeName)                     \
    bool is##TypeName() const;            \
    TypeName *to##TypeName();             \
    const TypeName *to##TypeName() const;
  #include "../defs/llc-instruction-tags.def"
  #undef X
};

struct StoreInst : public Instruction {
  Place *place;
  Value *value;
  StoreInst(Place *place, Value *value) : Instruction(Tag::StoreInst), place(place), value(value) { }
};

struct VoidCallInst : public Instruction {
  Call call;
  VoidCallInst(Call call) : Instruction(Tag::VoidCallInst), call(std::move(call)) { }
};

struct VarDeclInst : public Instruction {
  Variable *var;
  VarDeclInst(Variable *var) : Instruction(Tag::VarDeclInst), var(var) { }
};

// Value instructions, whose execution yields some result

struct ValueInst : public Instruction {
  // The variable (register) where the instruction stores its result
  Variable *reg;
  ValueInst(Tag tag, Variable *reg) : Instruction(tag), reg(reg) { }
};

enum class UnaryOp {
  Minus,
  Not,
  BitNot
};

std::string unaryOpToString(UnaryOp op);

struct UnaryOpInst : public ValueInst {
  UnaryOp op;
  Value *operand;
  UnaryOpInst(Variable *reg, UnaryOp op, Value *operand)
      : ValueInst(Tag::UnaryOpInst, reg), op(op), operand(operand) { }
};

struct AddressOfInst : public ValueInst {
  Place *place;
  AddressOfInst(Variable *reg, Place *place) : ValueInst(Tag::AddressOfInst, reg), place(place) { }
};

struct LoadInst : public ValueInst {
  Place *place;
  LoadInst(Variable *reg, Place *place) : ValueInst(Tag::LoadInst, reg), place(place) { }
};

enum class BinaryOp {
  Add,
  Sub,
  Mul,
  Div,
  Rem,
  LT,
  LE,
  GT,
  GE,
  EQ,
  NE,
  BitAnd,
  BitOr,
  BitXor,
  SHL,
  SHR
};

std::string binaryOpToString(BinaryOp op);

struct BinaryOpInst : public ValueInst {
  BinaryOp op;
  Value *left;
  Value *right;

  BinaryOpInst(Variable *reg, BinaryOp op, Value *left, Value *right)
      : ValueInst(Tag::BinaryOpInst, reg), op(op), left(left), right(right) { }
};

struct CallInst : public ValueInst {
  Call call;
  CallInst(Variable *reg, Call call) : ValueInst(Tag::CallInst, reg), call(std::move(call)) { }
};

struct CastInst : public ValueInst {
  Value *value;
  Type *target;
  CastInst(Variable *reg, Value *value, Type *target)
      : ValueInst(Tag::CastInst, reg), value(value), target(target) { }
};

// Terminators

struct Terminator {
  enum class Tag {
    #define X(TypeName) TypeName,
    #include "../defs/llc-terminator-tags.def"
    #undef X
  };

  Tag tag;

  Terminator(Tag tag) : tag(tag) { }
  virtual ~Terminator() = default;

  #define X(TypeName)                     \
    bool is##TypeName() const;            \
    TypeName *to##TypeName();             \
    const TypeName *to##TypeName() const;
  #include "../defs/llc-terminator-tags.def"
  #undef X
};

struct GotoTerm : public Terminator {
  BasicBlock *target;
  GotoTerm(BasicBlock *target) : Terminator(Tag::GotoTerm), target(target) { }
};

struct IfTerm : public Terminator {
  Value *cond;
  BasicBlock *then_block;
  BasicBlock *else_block;

  IfTerm(Value *cond, BasicBlock *then_block, BasicBlock *else_block)
      : Terminator(Tag::IfTerm), cond(cond), then_block(then_block), else_block(else_block) { }
};

struct ReturnTerm : public Terminator {
  Value *value;
  ReturnTerm(Value *value) : Terminator(Tag::ReturnTerm), value(value) { }
};

struct SwitchClause {
  Value *label;
  BasicBlock *target;
  SwitchClause(Value *label, BasicBlock *target) : label(label), target(target) { }
};

struct SwitchTerm : public Terminator {
  Value *cond;
  std::vector<SwitchClause> clauses;
  BasicBlock *default_target;

  SwitchTerm(Value *cond, std::vector<SwitchClause> clauses, BasicBlock *default_target)
      : Terminator(Tag::SwitchTerm), cond(cond), clauses(std::move(clauses)), default_target(default_target) { }
};

struct CallTerm : public Terminator {
  Call call;
  CallTerm(Call call) : Terminator(Tag::CallTerm), call(std::move(call)) { }
};

// Place

struct Deref {
  Value *ptr;
  Value *offset;
  Deref(Value *ptr, Value *offset) : ptr(ptr), offset(offset) { }
};

struct PlaceBase : public variant<PlaceBase, Variable *, Parameter *, Deref> {
  enum class Tag { Variable, Parameter, Deref };
  using variant::variant;

  const Type *getType() const;
};

struct Projection : public variant<Projection, Field *, Value *> {
  enum class Tag { Field, Index };
  using variant::variant;
};

struct Place {
  PlaceBase base;
  std::vector<Projection> projections;

  Place(PlaceBase base) : base(base) { }
  Place(PlaceBase base, std::vector<Projection> projections) : base(base), projections(std::move(projections)) { }
  const Type *getType() const;
};

// Values

struct Value {
  enum class Tag {
    #define X(TypeName) TypeName,
    #include "../defs/llc-value-tags.def"
    #undef X
  };

  Tag tag;
  const Type *type;

  Value(Tag tag, const Type *type) : tag(tag), type(type) { }
  virtual ~Value() = default;

  #define X(TypeName)                     \
    bool is##TypeName() const;            \
    TypeName *to##TypeName();             \
    const TypeName *to##TypeName() const;
  #include "../defs/llc-value-tags.def"
  #undef X
};

struct RegisterValue : public Value {
  Variable *reg;
  RegisterValue(const Type *type, Variable *reg) : Value(Tag::RegisterValue, type), reg(reg) { }
};

struct IntConstantValue : public Value {
  mpz_class value;
  IntConstantValue(const Type *type, mpz_class value) : Value(Tag::IntConstantValue, type), value(std::move(value)) { }
};

struct BoolConstantValue : public Value {
  bool value;
  BoolConstantValue(const Type *type, bool value) : Value(Tag::BoolConstantValue, type), value(value) { }
};

struct CharConstantValue : public Value {
  char value;
  CharConstantValue(const Type *type, char value) : Value(Tag::CharConstantValue, type), value(value) { }
};

struct EnumConstantValue : public Value {
  EnumValue *value;
  EnumConstantValue(const Type *type, EnumValue *value) : Value(Tag::EnumConstantValue, type), value(value) { }
};

struct NullValue : public Value {
  NullValue(const Type *type) : Value(Tag::NullValue, type) { }
};

struct SizeofValue : public Value {
  const Type *operand;
  SizeofValue(const Type *type, const Type *operand) : Value(Tag::SizeofValue, type), operand(operand) { }
};

struct GlobalStringValue : public Value {
  GlobalStringConstant *constant;
  GlobalStringValue(const Type *type, GlobalStringConstant *constant) : Value(Tag::GlobalStringValue, type), constant(constant) { }
};

}
