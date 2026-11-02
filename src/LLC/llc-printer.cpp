#include "llc-printer.hpp"

#include <cassert>
#include <iostream>
#include <variant>

#include "../util/util.hpp"

void LLCPrinter::indent() const {
  for (int i = 0; i < this->tabsize; ++i)
    this->os << " ";
}

void LLCPrinter::visit(const llc::Module *module) {
  std::string prelude = "typedef unsigned char      uint8;\n"
                        "typedef signed char        int8;\n"
                        "typedef unsigned short     uint16;\n"
                        "typedef short              int16;\n"
                        "typedef unsigned int       uint32;\n"
                        "typedef int                int32;\n"
                        "typedef unsigned long long uint64;\n"
                        "typedef long long          int64;\n"
                        "typedef _Bool bool;\n"
                        "typedef void* ptr;\n"
                        "#define true  1\n"
                        "#define false 0\n"
                        "#define NULL ((void*)0)\n"
                        "#define Load(x) (x)\n"
                        "#define Store(x, y) ((x) = (y))\n"
                        "#define BeginStorage(x, T) T x\n";

  this->os << prelude << "\n";

  for (const llc::GlobalStringConstant *string_constant : module->string_constants) {
    this->os << "char "
             << string_constant->global->name
             << "["
             << string_constant->value.size()
             << "] = "
             << stringValueToLiteral(string_constant->value)
             << ";\n";
  }

  if (!module->string_constants.empty())
    this->os << "\n";

  bool printed_type = false;

  for (const llc::Type *type : module->types) {
    if (type->isStructType() || type->isUnionType()) {
      this->printAsTypeDecl(type);
      printed_type = true;
    }
  }

  if (printed_type)
    this->os << "\n";

  std::unordered_set<const llc::Type *> visited;
  for (llc::Type *type : module->types)
    this->printType(type, &visited);

  for (const llc::Function *function : module->functions)
    this->printAsDeclaration(function);

  this->os << "\n";

  for (size_t i = 0, n = module->functions.size(); i < n; ++i) {
    // TODO
    if (module->functions[i]->is_extern)
      continue;

    this->printAsDefinition(module->functions[i]);
    if (i < n - 1)
      this->os << "\n";
  }
}

void LLCPrinter::printAsDeclaration(const llc::Function *function) const {
  this->printFunctionHeader(function);
  this->os << ";\n";
}

void LLCPrinter::printAsDefinition(const llc::Function *func) const {
  this->printFunctionHeader(func);

  this->os << " {\n";

  // assert(!func->blocks.empty());

  for (const llc::BasicBlock *block : func->blocks)
    this->visit(block);

  this->os << "}\n";
}

void LLCPrinter::printFunctionHeader(const llc::Function *func) const {
  if (func->is_extern)
    this->os << "extern ";

  if (func->return_type == nullptr)
    this->os << "void";
  else
    this->printAsTypeName(func->return_type);

  this->os << " " << func->name << "(";

  for (size_t i = 0, n = func->params.size(); i < n; ++i) {
    this->printAsTypeName(func->params[i]->type);
    this->os << " " << func->params[i]->name;
    if (i < n - 1)
      this->os << ", ";
  }

  this->os << ")";
}

void LLCPrinter::visit(const llc::BasicBlock *block) const {
  this->os << "bb" << block->id << ":;\n";

  for (const llc::Instruction *inst : block->insts)
    this->visit(inst);

  assert(block->terminator != nullptr);
  this->visit(block->terminator);
}

void LLCPrinter::printType(const llc::Type *type, std::unordered_set<const llc::Type *> *visited) {
  if (visited->count(type))
    return;

  visited->insert(type);

  for (const llc::Type *dependency : type->dependencies())
    this->printType(dependency, visited);

  this->printAsTypeDef(type);
  this->os << "\n";
}

void LLCPrinter::printAsTypeDecl(const llc::Type *type) const {
  switch (type->tag) {
  case llc::Type::Tag::StructType: {
    this->os << "struct " << type->toStructType()->name << ";\n";
    break;
  }
  case llc::Type::Tag::UnionType: {
    this->os << "union " << type->toUnionType()->name << ";\n";
    break;
  }
  default:
    assert(false);
  }
}

void LLCPrinter::printAsTypeDef(const llc::Type *type) const {
  switch (type->tag) {
  case llc::Type::Tag::StructType: {
    this->printStructOrEnumDecl("struct", type->toStructType()->name, type->toStructType()->fields);
    break;
  }
  case llc::Type::Tag::UnionType: {
    this->printStructOrEnumDecl("union", type->toUnionType()->name, type->toUnionType()->fields);
    break;
  }
  case llc::Type::Tag::EnumType: {
    this->os << "enum " << type->toEnumType()->name << " {\n";

    for (size_t i = 0, n = type->toEnumType()->values.size(); i < n; ++i) {
      this->indent();
      this->os << type->toEnumType()->values[i]->name;
      if (i < n - 1)
        this->os << ",\n";
    }

    this->os << "\n};\n";
    break;
  }
  default:
    assert(false && "type has no type decl");
  }
}

void LLCPrinter::printStructOrEnumDecl(std::string_view keyword,
                                      std::string_view name,
                                      const std::vector<llc::Field *> &fields) const {

  this->os << keyword << " " << name << " {\n";

  for (size_t i = 0, n = fields.size(); i < n; ++i) {
    this->indent();
    if (fields[i]->type->isArrayType()) {
      this->printAsTypeName(fields[i]->type->toArrayType()->element);
      this->os << " " << fields[i]->name << "[" << fields[i]->type->toArrayType()->length << "];\n";
    } else {
      this->printAsTypeName(fields[i]->type);
      this->os << " " << fields[i]->name << ";\n";
    }
  }

  this->os << "};\n";
}

void LLCPrinter::printAsTypeName(const llc::Type *type) const {
  switch (type->tag) {
  case llc::Type::Tag::IntegerType: {
    this->os << (type->toIntegerType()->is_signed ? "int" : "uint") << type->toIntegerType()->bits;
    break;
  }
  case llc::Type::Tag::BoolType: {
    this->os << "bool";
    break;
  }
  case llc::Type::Tag::CharType: {
    this->os << "char";
    break;
  }
  case llc::Type::Tag::PtrType: {
    this->os << "ptr";
    break;
  }
  case llc::Type::Tag::PointerType: {
    this->printAsTypeName(type->toPointerType()->pointee);
    this->os << "*";
    break;
  }
  case llc::Type::Tag::ArrayType: {
    assert(false);
    break;
  }
  case llc::Type::Tag::StructType: {
    this->os << "struct " << type->toStructType()->name;
    break;
  }
  case llc::Type::Tag::UnionType: {
    this->os << "union " << type->toUnionType()->name;
    break;
  }
  case llc::Type::Tag::EnumType: {
    this->os << "enum " << type->toEnumType()->name;
    break;
  }
  default:
    assert(false);
  }
}

void LLCPrinter::visit(const llc::Instruction *inst) const {
  this->indent();

  switch (inst->tag) {
  case llc::Instruction::Tag::StoreInst: {
    this->os << "Store(";
    this->visit(inst->toStoreInst()->place);
    this->os << ", ";
    this->visit(inst->toStoreInst()->value);
    this->os << ");\n";
    break;
  }
  case llc::Instruction::Tag::VoidCallInst: {
    this->visit(&inst->toVoidCallInst()->call);
    this->os << ";\n";
    break;
  }
  case llc::Instruction::Tag::VarDeclInst: {
    this->os << "BeginStorage(";
    this->os << inst->toVarDeclInst()->var->name << ", ";
    this->printAsTypeName(inst->toVarDeclInst()->var->type);
    this->os << ");\n";
    break;
  }
  case llc::Instruction::Tag::UnaryOpInst: {
    this->printRegDecl(inst->toUnaryOpInst()->reg);
    this->os << " = " << llc::unaryOpToString(inst->toUnaryOpInst()->op);
    this->visit(inst->toUnaryOpInst()->operand);
    this->os << ";\n";
    break;
  }
  case llc::Instruction::Tag::AddressOfInst: {
    this->printRegDecl(inst->toAddressOfInst()->reg);
    this->os << " = &";
    this->visit(inst->toAddressOfInst()->place);
    this->os << ";\n";
    break;
  }
  case llc::Instruction::Tag::LoadInst: {
    this->printRegDecl(inst->toLoadInst()->reg);
    this->os << " = Load(";
    this->visit(inst->toLoadInst()->place);
    this->os << ");\n";
    break;
  }
  case llc::Instruction::Tag::BinaryOpInst: {
    this->printRegDecl(inst->toBinaryOpInst()->reg);
    this->os << " = ";
    this->visit(inst->toBinaryOpInst()->left);
    this->os << " " << llc::binaryOpToString(inst->toBinaryOpInst()->op) << " ";
    this->visit(inst->toBinaryOpInst()->right);
    this->os << ";\n";
    break;
  }
  case llc::Instruction::Tag::CallInst: {
    this->printRegDecl(inst->toCallInst()->reg);
    this->os << " = ";
    this->visit(&inst->toCallInst()->call);
    this->os << ";\n";
    break;
  }
  case llc::Instruction::Tag::CastInst: {
    this->printRegDecl(inst->toCastInst()->reg);
    this->os << " = (";
    this->printAsTypeName(inst->toCastInst()->target);
    this->os << ")";
    this->visit(inst->toCastInst()->value);
    this->os << ";\n";
    break;
  }
  default:
    assert(false);
  }
}

void LLCPrinter::printRegDecl(const llc::Variable *reg) const {
  this->printAsTypeName(reg->type);
  this->os << " " << reg->name;
}

void LLCPrinter::visit(const llc::Call *call) const {
  this->os << call->func->name << "(";

  for (size_t i = 0, n = call->args.size(); i < n; ++i) {
    this->visit(call->args[i]);
    if (i < n - 1)
      this->os << ", ";
  }

  this->os << ")";
}

void LLCPrinter::visit(const llc::Terminator *term) const {
  this->indent();

  switch (term->tag) {
  case llc::Terminator::Tag::GotoTerm: {
    this->os << "goto bb" << term->toGotoTerm()->target->id << ";\n";
    break;
  }
  case llc::Terminator::Tag::IfTerm: {
    this->os << "if (";
    this->visit(term->toIfTerm()->cond);
    this->os << ") goto bb" << term->toIfTerm()->then_block->id
             << "; else goto bb" << term->toIfTerm()->else_block->id << ";\n";
    break;
  }
  case llc::Terminator::Tag::ReturnTerm: {
    this->os << "return";

    if (term->toReturnTerm()->value != nullptr) {
      this->os << " ";
      this->visit(term->toReturnTerm()->value);
    }

    this->os << ";\n";
    break;
  }
  case llc::Terminator::Tag::SwitchTerm: {
    this->os << "switch (";
    this->visit(term->toSwitchTerm()->cond);
    this->os << ") {\n";

    for (const llc::SwitchClause &clause : term->toSwitchTerm()->clauses) {
      this->indent();
      this->os << "case ";
      this->visit(clause.label);
      this->os << ": goto bb" << clause.target->id << ";\n";
    }

    this->indent();
    this->os << "default: goto bb" << term->toSwitchTerm()->default_target->id << ";\n";

    this->indent();
    this->os << "}\n";
    break;
  }
  case llc::Terminator::Tag::CallTerm: {
    this->visit(&term->toCallTerm()->call);
    this->os << ";\n";
    break;
  }
  default:
    assert(false);
  }
}

void LLCPrinter::visit(const llc::Place *place) const {
  this->visit(&place->base);

  for (const llc::Projection &proj : place->projections)
    this->visit(proj);
}

void LLCPrinter::visit(const llc::PlaceBase *base) const {
  switch (base->getTag()) {
  case llc::PlaceBase::Tag::Variable: {
    this->os << base->get<llc::Variable *>()->name;
    break;
  }
  case llc::PlaceBase::Tag::Parameter: {
    this->os << base->get<llc::Parameter *>()->name;
    break;
  }
  case llc::PlaceBase::Tag::Deref: {
    const llc::Deref &deref = base->get<llc::Deref>();
    if (deref.offset == nullptr) {
      this->os << "(*";
      this->visit(deref.ptr);
      this->os << ")";
    } else {
      this->visit(deref.ptr);
      this->os << "[";
      this->visit(deref.offset);
      this->os << "]";
    }

    break;
  }
  default:
    assert(false);
  }
}

void LLCPrinter::visit(const llc::Projection &proj) const {
  switch (proj.getTag()) {
  case llc::Projection::Tag::Field: {
    this->os << "." << proj.get<llc::Field *>()->name;
    break;
  }
  case llc::Projection::Tag::Index: {
    this->os << "[";
    this->visit(proj.get<llc::Value *>());
    this->os << "]";
    break;
  }
  default:
    assert(false);
  }
}

void LLCPrinter::visit(const llc::Value *value) const {
  switch (value->tag) {
  case llc::Value::Tag::RegisterValue: {
    this->os << value->toRegisterValue()->reg->name;
    break;
  }
  case llc::Value::Tag::IntConstantValue: {
    this->os << value->toIntConstantValue()->value;
    if (!value->type->toIntegerType()->is_signed)
      this->os << "U";
    break;
  }
  case llc::Value::Tag::BoolConstantValue: {
    this->os << (value->toBoolConstantValue()->value ? "true" : "false");
    break;
  }
  case llc::Value::Tag::CharConstantValue: {
    this->os << charValueToLiteral(value->toCharConstantValue()->value);
    break;
  }
  case llc::Value::Tag::EnumConstantValue: {
    this->os << value->toEnumConstantValue()->value->name;
    break;
  }
  case llc::Value::Tag::NullValue: {
    this->os << "NULL";
    break;
  }
  case llc::Value::Tag::SizeofValue: {
    this->os << "sizeof(";
    this->printAsTypeName(value->toSizeofValue()->operand);
    this->os << ")";
    break;
  }
  case llc::Value::Tag::GlobalStringValue: {
    this->os << value->toGlobalStringValue()->constant->global->name;
    break;
  }
  default:
    assert(false);
  }
}
