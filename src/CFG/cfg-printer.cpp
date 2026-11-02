#include "cfg-printer.hpp"

#include "../frontend/acl.hpp"
#include "../frontend/type.hpp"

void CFGPrinter::indent() const {
  for (int i = 0; i < this->tabsize; ++i)
    this->os << " ";
}

void CFGPrinter::visit(const std::vector<Function *> &funcs, const std::unordered_map<const Function *, cfg::CFG> &cfg_map) const {
  std::vector<Function *> filtered;

  std::copy_if(funcs.begin(), funcs.end(), std::back_inserter(filtered),
    [](Function *func) {
      return func->body != nullptr;
    }
  );

  for (size_t i = 0, n = filtered.size(); i < n; ++i) {
    const cfg::CFG &cfg = cfg_map.at(filtered[i]);
    this->visit(filtered[i], cfg);

    if (i < n - 1)
      this->os << "\n";
  }
}

void CFGPrinter::printFunctionName(const Function *func) const {
  if (func->attachedTo() != nullptr)
    this->os << func->attachedTo()->toIdentifier() << ".";

  this->os << func->name;
}

void CFGPrinter::visit(const Function *func, const cfg::CFG &cfg) const {
  this->os << "function ";
  this->printFunctionName(func);
  this->os << " {\n";

  for (const cfg::BasicBlock *block : cfg.blocks)
    this->visit(block);

  this->os << "}\n";
}

void CFGPrinter::visit(const cfg::BasicBlock *block) const {
  this->os << "bb" << block->id << ":";
  this->printBlockInfo(block);

  for (const cfg::Instruction *inst : block->insts)
    this->visit(inst);

  assert(block->term != nullptr);
  this->visit(block->term);
}

void CFGPrinter::printBlockInfo(const cfg::BasicBlock *block) const {
  this->os << "\t\t\t\t\tpreds: ";
  this->printBlockSet(block->predecessors);
  this->os << ", succs: ";
  this->printBlockSet(block->successors);
  this->os << "\n";
}

void CFGPrinter::printBlockSet(const std::vector<cfg::BasicBlock *> &blocks) const {
  this->os << "{";

  for (size_t i = 0, n = blocks.size(); i < n; ++i) {
    this->os << "bb" << blocks[i]->id;

    if (i < n - 1)
      this->os << ", ";
  }

  this->os << "}";
}

void CFGPrinter::visit(const cfg::Instruction *inst) const {
  this->indent();

  switch (inst->tag) {
  case cfg::Instruction::Tag::VoidCallInst: {
    this->visit(&inst->toVoidCallInst()->call);
    this->os << "\n";
    break;
  }
  case cfg::Instruction::Tag::BeginStorageInst: {
    this->os << "BeginStorage(" << inst->toBeginStorageInst()->storage.toString()
             << ", " << inst->toBeginStorageInst()->storage.getType()->toString() << ")\n";
    break;
  }
  case cfg::Instruction::Tag::EndStorageInst: {
    this->os << "EndStorage(" << inst->toEndStorageInst()->storage.toString() << ")\n";
    break;
  }
  case cfg::Instruction::Tag::DropValInst: {
    this->os << "DropVal(" << "%" << inst->toDropValInst()->reg->id << ")\n";
    break;
  }
  case cfg::Instruction::Tag::InitPlaceInst: {
    this->os << "InitPlace(";
    this->visit(inst->toInitPlaceInst()->place);
    this->os << ", ";
    this->visit(inst->toInitPlaceInst()->value);
    this->os << ")\n";
    break;
  }
  case cfg::Instruction::Tag::AssignPlaceInst: {
    this->os << "AssignPlace(";
    this->visit(inst->toAssignPlaceInst()->place);
    this->os << ", ";
    this->visit(inst->toAssignPlaceInst()->value);
    this->os << ")\n";
    break;
  }
  case cfg::Instruction::Tag::UnaryOpInst: {
    this->os << "%" << inst->toUnaryOpInst()->reg->id << " " << inst->toUnaryOpInst()->reg->type->toString() << " = ";
    this->os << cfg::unaryOpToString(inst->toUnaryOpInst()->op);
    this->visit(inst->toUnaryOpInst()->operand);
    this->os << "\n";
    break;
  }
  case cfg::Instruction::Tag::AddressOfInst: {
    this->os << "%" << inst->toAddressOfInst()->reg->id << " " << inst->toAddressOfInst()->reg->type->toString() << " = ";
    this->os << "&";
    this->visit(inst->toAddressOfInst()->place);
    this->os << "\n";
    break;
  }
  case cfg::Instruction::Tag::LoadInst: {
    this->os << "%" << inst->toLoadInst()->reg->id << " " << inst->toLoadInst()->reg->type->toString() << " = ";
    this->os << "Load(";
    this->visit(inst->toLoadInst()->place);
    this->os << ")\n";
    break;
  }
  case cfg::Instruction::Tag::TakeInst: {
    this->os << "%" << inst->toTakeInst()->reg->id << " " << inst->toTakeInst()->reg->type->toString() << " = ";
    this->os << "Take(";
    this->visit(inst->toTakeInst()->place);
    this->os << ")\n";
    break;
  }
  case cfg::Instruction::Tag::CallInst: {
    this->os << "%" << inst->toCallInst()->reg->id << " " << inst->toCallInst()->reg->type->toString() << " = ";
    this->visit(&inst->toCallInst()->call);
    this->os << "\n";
    break;
  }
  case cfg::Instruction::Tag::BinaryOpInst: {
    this->os << "%" << inst->toBinaryOpInst()->reg->id << " " << inst->toBinaryOpInst()->reg->type->toString() << " = ";
    this->visit(inst->toBinaryOpInst()->left);
    this->os << " " << cfg::binaryOpToString(inst->toBinaryOpInst()->op) << " ";
    this->visit(inst->toBinaryOpInst()->right);
    this->os << "\n";
    break;
  }
  case cfg::Instruction::Tag::CompositeLiteralInst: {
    this->os << "%" << inst->toCompositeLiteralInst()->reg->id << " " << inst->toCompositeLiteralInst()->reg->type->toString() << " = ";
    this->os << "{";
    const std::vector<cfg::Value *> &components = inst->toCompositeLiteralInst()->components;
    for (size_t i = 0, n = components.size(); i < n; ++i) {
      this->visit(components[i]);
      if (i < n - 1)
        this->os << ", ";
    }
    this->os << "}\n";
    break;
  }
  case cfg::Instruction::Tag::NewInst: {
    this->os << "%" << inst->toNewInst()->reg->id << " " << inst->toNewInst()->reg->type->toString() << " = ";
    this->os << "New(" << inst->toNewInst()->alloc_type->toString() << ", ";
    this->visit(inst->toNewInst()->value);
    this->os << ")\n";
    break;
  }
  case cfg::Instruction::Tag::PropertyAccessInst: {
    this->os << "%" << inst->toPropertyAccessInst()->reg->id << " " << inst->toPropertyAccessInst()->reg->type->toString() << " = ";
    this->os << "PropertyAccess(";
    this->visit(inst->toPropertyAccessInst()->base);
    this->os << ", " << inst->toPropertyAccessInst()->property->name << ")\n";
    break;
  }
  case cfg::Instruction::Tag::TypeTestInst: {
    this->os << "%" << inst->toTypeTestInst()->reg->id << " " << inst->toTypeTestInst()->reg->type->toString() << " = ";
    this->visit(inst->toTypeTestInst()->place);
    this->os << " is " << (inst->toTypeTestInst()->is_negated ? "not " : "") << inst->toTypeTestInst()->alternative->type->toString() << "\n";
    break;
  }
  case cfg::Instruction::Tag::ValueCastInst: {
    this->os << "%" << inst->toValueCastInst()->reg->id << " " << inst->toValueCastInst()->reg->type->toString() << " = ";
    this->os << "ValueCast(";
    this->visit(inst->toValueCastInst()->value);
    this->os << ", " << inst->toValueCastInst()->target->toString() << ")\n";
    break;
  }
  case cfg::Instruction::Tag::PlaceCastInst: {
    this->os << "%" << inst->toPlaceCastInst()->reg->id << " " << inst->toPlaceCastInst()->reg->type->toString() << " = ";
    this->os << "PlaceCast(";
    this->visit(inst->toPlaceCastInst()->place);
    this->os << ", " << inst->toPlaceCastInst()->target->toString() << ")\n";
    break;
  }
  default:
    assert(false);
  }
}

void CFGPrinter::visit(const cfg::Call *call) const {
  this->printFunctionName(call->func);

  this->os << "(";

  for (size_t i = 0, n = call->args.size(); i < n; ++i) {
    this->visit(call->args[i]);
    if (i < n - 1 || !call->out_args.empty())
      this->os << ", ";
  }

  for (size_t i = 0, n = call->out_args.size(); i < n; ++i) {
    this->os << "out ";
    this->visit(call->out_args[i]);
    if (i < n - 1)
      this->os << ", ";
  }

  this->os << ")";
}

void CFGPrinter::visit(const cfg::Terminator *term) const {
  this->indent();

  switch (term->tag) {
  case cfg::Terminator::Tag::GotoTerm: {
    this->os << "goto " << "bb" << term->toGotoTerm()->target->id << "\n";
    break;
  }
  case cfg::Terminator::Tag::IfTerm: {
    this->os << "if (";
    this->visit(term->toIfTerm()->cond);
    this->os << ") goto "
             << "bb" << term->toIfTerm()->then_bb->id << " else goto "
             << "bb" << term->toIfTerm()->else_bb->id << "\n";
    break;
  }
  case cfg::Terminator::Tag::ReturnTerm: {
    this->os << "return";

    if (term->toReturnTerm()->value != nullptr) {
      this->os << " ";
      this->visit(term->toReturnTerm()->value);
    }

    this->os << "\n";
    break;
  }
  case cfg::Terminator::Tag::SwitchTerm: {
    this->os << "switch (";
    this->visit(term->toSwitchTerm()->cond);
    this->os << ") {\n";

    for (const cfg::SwitchClause &clause : term->toSwitchTerm()->clauses) {
      this->indent();
      this->os << "case ";
      this->visit(clause.label);
      this->os << ": goto " << "bb" << clause.target->id << ";\n";
    }

    if (term->toSwitchTerm()->default_target != nullptr) {
      this->indent();
      this->os << "default: goto " << "bb" << term->toSwitchTerm()->default_target->id << ";\n";
    }

    this->indent();
    this->os << "}\n";
    break;
  }
  case cfg::Terminator::Tag::VariantSwitchTerm: {
    this->os << "switch (";
    this->visit(term->toVariantSwitchTerm()->cond);
    this->os << ") {\n";

    for (const cfg::VariantSwitchClause &clause : term->toVariantSwitchTerm()->clauses) {
      this->indent();
      this->os << "case " << clause.label->type->toString() << ": goto " << "bb" << clause.target->id << ";\n";
    }

    if (term->toVariantSwitchTerm()->default_target != nullptr) {
      this->indent();
      this->os << "default: goto " << "bb" << term->toVariantSwitchTerm()->default_target->id << ";\n";
    }

    this->indent();
    this->os << "}\n";
    break;
  }
  case cfg::Terminator::Tag::PanicTerm: {
    this->os << "Panic(" << stringValueToLiteral(term->toPanicTerm()->message) << ")\n";
    break;
  }
  case cfg::Terminator::Tag::CallTerm: {
    this->visit(&term->toCallTerm()->call);
    this->os << "\n";
    break;
  }
  default:
    assert(false);
  }
}

void CFGPrinter::visit(const cfg::Place *place) const {
  this->visit(&place->base);

  for (const cfg::Projection &proj : place->projections)
    this->visit(proj);
}

void CFGPrinter::visit(const cfg::PlaceBase *base) const {
  switch (base->getTag()) {
  case cfg::PlaceBase::Tag::Variable: {
    this->os << base->get<Variable *>()->name;
    break;
  }
  case cfg::PlaceBase::Tag::Parameter: {
    if (base->get<Parameter *>()->isNamed())
      this->os << base->get<Parameter *>()->getName();
    else
      this->os << "<unnamed-ret-param>";
    break;
  }
  case cfg::PlaceBase::Tag::Temporary: {
    this->os << "_t" << base->get<cfg::Temporary *>()->id;
    break;
  }
  case cfg::PlaceBase::Tag::Deref: {
    this->os << "(*";
    this->visit(base->get<cfg::Deref>().ptr);
    this->os << ")";
    break;
  }
  default:
    assert(false);
  }
}

void CFGPrinter::visit(const cfg::Projection &proj) const {
  switch (proj.getTag()) {
  case cfg::Projection::Tag::StructField: {
    this->os << "." << proj.get<StructField *>()->name;
    break;
  }
  case cfg::Projection::Tag::Index: {
    this->os << "[";
    this->visit(proj.get<cfg::Value *>());
    this->os << "]";
    break;
  }
  case cfg::Projection::Tag::VariantAlternative: {
    this->os << ".(" << proj.get<VariantAlternative *>()->type->toString() << ")";
    break;
  }
  default:
    assert(false);
  }
}

void CFGPrinter::visit(const cfg::Value *value) const {
  switch (value->tag) {
  case cfg::Value::Tag::RegisterValue: {
    this->os << "%" << value->toRegisterValue()->reg->id;
    break;
  }
  case cfg::Value::Tag::ConstantValue: {
    this->os << value->toConstantValue()->constant->toString() << " " << value->toConstantValue()->type->toString();
    break;
  }
  default:
    assert(false);
  }
}
