#pragma once

#include <cassert>
#include <string>
#include <vector>

#include "gmpxx.h"
#include "../frontend/source-location.hpp"
#include "../util/util.hpp"

struct Function;
struct Parameter;
struct Variable;
struct StructField;
struct VariantAlternative;
struct Property;
struct Constant;
class Type;

namespace cfg {

struct BasicBlock;

struct Instruction;
struct VoidCallInst;
struct BeginStorageInst;
struct EndStorageInst;
struct DropValInst;
struct InitPlaceInst;
struct AssignPlaceInst;
struct UnaryOpInst;
struct AddressOfInst;
struct LoadInst;
struct TakeInst;
struct CallInst;
struct BinaryOpInst;
struct CompositeLiteralInst;
struct NewInst;
struct PropertyAccessInst;
struct TypeTestInst;
struct ValueCastInst;
struct PlaceCastInst;

struct Terminator;
struct GotoTerm;
struct IfTerm;
struct ReturnTerm;
struct SwitchTerm;
struct VariantSwitchTerm;
struct PanicTerm;
struct CallTerm;

struct Place;

struct Value;
struct RegisterValue;
struct ConstantValue;

struct CFG {
  // blocks[0] is the entry point
  std::vector<BasicBlock *> blocks;

  CFG(std::vector<BasicBlock *> blocks) : blocks(std::move(blocks)) { }

  void buildEdges();
  void addEdge(cfg::BasicBlock *from_bb, cfg::BasicBlock *to_bb);
};

struct Temporary {
  unsigned id;
  const Type *type;
  Temporary(unsigned id, const Type *type) : id(id), type(type) { }
};

struct Register {
  unsigned id;
  const Type *type;
  Register(unsigned id, const Type *type) : id(id), type(type) { }
};

struct BasicBlock {
  unsigned id;
  std::vector<Instruction *> insts;
  Terminator *term = nullptr;

  std::vector<BasicBlock *> successors;
  std::vector<BasicBlock *> predecessors;

  BasicBlock(unsigned id) : id(id) { }

  void setTerminator(Terminator *term) {
    assert(this->term == nullptr);
    this->term = term;
  }
};

struct Storage : public variant<Storage, Variable *, Parameter *, Temporary *> {
  enum class Tag { Variable, Parameter, Temporary };
  using variant::variant;

  const Type *getType() const;
  std::string toString() const;
};

// Instructions

struct Instruction {
  enum class Tag {
    #define X(TypeName) TypeName,
    #include "../defs/cfg-instruction-tags.def"
    #undef X
  };

  Tag tag;

  Instruction(Tag tag) : tag(tag) { }
  virtual ~Instruction() = default;

  #define X(TypeName)                     \
    bool is##TypeName() const;            \
    TypeName *to##TypeName();             \
    const TypeName *to##TypeName() const;
  #include "../defs/cfg-instruction-tags.def"
  #undef X
};

struct Call {
  Function *func;
  std::vector<Value *> args;
  std::vector<Place *> out_args;
  SourceLocation loc;

  Call(Function *func, std::vector<Value *> args, std::vector<Place *> out_args, SourceLocation loc)
      : func(func), args(std::move(args)), out_args(std::move(out_args)), loc(loc) { }
};

struct VoidCallInst : public Instruction {
  Call call;
  VoidCallInst(Call call) : Instruction(Tag::VoidCallInst), call(std::move(call)) { }
};

struct BeginStorageInst : public Instruction {
  Storage storage;
  BeginStorageInst(Storage storage) : Instruction(Tag::BeginStorageInst), storage(storage) { }
};

struct EndStorageInst : public Instruction {
  Storage storage;
  EndStorageInst(Storage storage) : Instruction(Tag::EndStorageInst), storage(storage) { }
};

struct DropValInst : public Instruction {
  Register *reg;
  DropValInst(Register *reg) : Instruction(Tag::DropValInst), reg(reg) { }
};

struct InitPlaceInst : public Instruction {
  Place *place;
  Value *value;
  InitPlaceInst(Place *place, Value *value) : Instruction(Tag::InitPlaceInst), place(place), value(value) { }
};

struct AssignPlaceInst : public Instruction {
  Place *place;
  Value *value;
  AssignPlaceInst(Place *place, Value *value) : Instruction(Tag::AssignPlaceInst), place(place), value(value) { }
};

// Value instructions, whose execution yields some result

struct ValueInst : public Instruction {
  // The register where the instruction stores its result
  Register *reg;
  ValueInst(Tag tag, Register *reg) : Instruction(tag), reg(reg) { }
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
  UnaryOpInst(Register *reg, UnaryOp op, Value *operand) : ValueInst(Tag::UnaryOpInst, reg), op(op), operand(operand) { }
};

struct AddressOfInst : public ValueInst {
  Place *place;
  AddressOfInst(Register *reg, Place *place) : ValueInst(Tag::AddressOfInst, reg), place(place) { }
};

struct LoadInst : public ValueInst {
  Place *place;
  LoadInst(Register *reg, Place *place) : ValueInst(Tag::LoadInst, reg), place(place) { }
};

struct TakeInst : public ValueInst {
  Place *place;
  TakeInst(Register *reg, Place *place) : ValueInst(Tag::TakeInst, reg), place(place) { }
};

struct CallInst : public ValueInst {
  Call call;
  CallInst(Register *reg, Call call) : ValueInst(Tag::CallInst, reg), call(std::move(call)) { }
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

  BinaryOpInst(Register *reg, BinaryOp op, Value *left, Value *right)
      : ValueInst(Tag::BinaryOpInst, reg), op(op), left(left), right(right) { }
};

struct CompositeLiteralInst : public ValueInst {
  std::vector<Value *> components;
  CompositeLiteralInst(Register *reg, std::vector<Value *> components)
      : ValueInst(Tag::CompositeLiteralInst, reg), components(std::move(components)) { }
};

struct NewInst : public ValueInst {
  const Type *alloc_type;  // The type to allocate memory for
  Value *value;  // The value to store in the allocated memory
  NewInst(Register *reg, const Type *alloc_type, Value *value) : ValueInst(Tag::NewInst, reg), alloc_type(alloc_type), value(value) { }
};

struct PropertyAccessInst : public ValueInst {
  Place *base;
  Property *property;
  PropertyAccessInst(Register *reg, Place *base, Property *property)
      : ValueInst(Tag::PropertyAccessInst, reg), base(base), property(property) { }
};

struct TypeTestInst : public ValueInst {
  Place *place;
  VariantAlternative *alternative;
  bool is_negated;
  TypeTestInst(Register *reg, Place *place, VariantAlternative *alternative, bool is_negated)
      : ValueInst(Tag::TypeTestInst, reg), place(place), alternative(alternative), is_negated(is_negated) { }
};

struct ValueCastInst : public ValueInst {
  Value *value;
  const Type *target;
  ValueCastInst(Register *reg, Value *value, const Type *target) : ValueInst(Tag::ValueCastInst, reg), value(value), target(target) { }
};

struct PlaceCastInst : public ValueInst {
  Place *place;
  const Type *target;
  PlaceCastInst(Register *reg, Place *place, const Type *target) : ValueInst(Tag::PlaceCastInst, reg), place(place), target(target) { }
};

// Terminators

struct Terminator {
  enum class Tag {
    #define X(TypeName) TypeName,
    #include "../defs/cfg-terminator-tags.def"
    #undef X
  };

  Tag tag;

  Terminator(Tag tag) : tag(tag) { }

  #define X(TypeName)                     \
    bool is##TypeName() const;            \
    TypeName *to##TypeName();             \
    const TypeName *to##TypeName() const;
  #include "../defs/cfg-terminator-tags.def"
  #undef X
};

struct GotoTerm : public Terminator {
  BasicBlock *target;
  GotoTerm(BasicBlock *target) : Terminator(Tag::GotoTerm), target(target) { }
};

struct IfTerm : public Terminator {
  Value *cond;
  BasicBlock *then_bb;
  BasicBlock *else_bb;

  IfTerm(Value *cond, BasicBlock *then_bb, BasicBlock *else_bb)
      : Terminator(Tag::IfTerm), cond(cond), then_bb(then_bb), else_bb(else_bb) { }
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
  SourceLocation loc;

  SwitchTerm(Value *cond, std::vector<SwitchClause> clauses, BasicBlock *default_target, SourceLocation loc)
      : Terminator(Tag::SwitchTerm), cond(cond), clauses(std::move(clauses)), default_target(default_target), loc(loc) { }
};

struct VariantSwitchClause {
  VariantAlternative *label;
  BasicBlock *target;
  VariantSwitchClause(VariantAlternative *label, BasicBlock *target) : label(label), target(target) { }
};

struct VariantSwitchTerm : public Terminator {
  Place *cond;
  std::vector<VariantSwitchClause> clauses;
  BasicBlock *default_target;
  SourceLocation loc;

  VariantSwitchTerm(Place *cond, std::vector<VariantSwitchClause> clauses, BasicBlock *default_target, SourceLocation loc)
      : Terminator(Tag::VariantSwitchTerm), cond(cond), clauses(std::move(clauses)), default_target(default_target), loc(loc) { }
};

struct PanicTerm : public Terminator {
  SourceLocation loc;
  std::string message;
  PanicTerm(SourceLocation loc, std::string message)
      : Terminator(Tag::PanicTerm), loc(loc), message(std::move(message)) { }
};

struct CallTerm : public Terminator {
  Call call;
  CallTerm(Call call) : Terminator(Tag::CallTerm), call(std::move(call)) { }
};

// Place

struct Deref {
  Deref(Value *ptr) : ptr(ptr) { }
  Value *ptr;
};

struct PlaceBase : public variant<PlaceBase, Variable *, Parameter *, Temporary *, Deref> {
  enum class Tag { Variable, Parameter, Temporary, Deref };
  using variant::variant;

  const Type *getType() const;
};

struct Projection : public variant<Projection, StructField *, Value *, VariantAlternative *> {
  enum class Tag { StructField, Index, VariantAlternative };
  using variant::variant;
};

struct Place {
  PlaceBase base;
  std::vector<Projection> projections;

  Place(PlaceBase base) : base(base) { }
  const Type *getType() const;
};

// Values

struct Value {
  enum class Tag {
    #define X(TypeName) TypeName,
    #include "../defs/cfg-value-tags.def"
    #undef X
  };

  Tag tag;

  Value(Tag tag) : tag(tag) { }
  virtual ~Value() = default;

  #define X(TypeName)                     \
    bool is##TypeName() const;            \
    TypeName *to##TypeName();             \
    const TypeName *to##TypeName() const;
  #include "../defs/cfg-value-tags.def"
  #undef X

  const Type *getType() const;
};

struct RegisterValue : public Value {
  Register *reg;
  RegisterValue(Register *reg) : Value(Tag::RegisterValue), reg(reg) { }
};

struct ConstantValue : public Value {
  const Constant *constant;
  const Type *type;
  ConstantValue(const Constant *constant, const Type *type) : Value(Tag::ConstantValue), constant(constant), type(type) { }
};

}
