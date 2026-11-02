#include "cfg.hpp"

#include <algorithm>
#include <cassert>

#include "../frontend/acl.hpp"
#include "../frontend/type.hpp"

void cfg::CFG::buildEdges() {
  for (BasicBlock *block : this->blocks) {
    cfg::Terminator *term = block->term;
    assert(term != nullptr);

    switch (term->tag) {
    case cfg::Terminator::Tag::GotoTerm: {
      this->addEdge(block, term->toGotoTerm()->target);
      continue;
    }
    case cfg::Terminator::Tag::IfTerm: {
      this->addEdge(block, term->toIfTerm()->then_bb);
      this->addEdge(block, term->toIfTerm()->else_bb);
      continue;
    }
    case cfg::Terminator::Tag::SwitchTerm: {
      for (cfg::SwitchClause &clause : term->toSwitchTerm()->clauses)
        this->addEdge(block, clause.target);

      cfg::BasicBlock *default_target = term->toSwitchTerm()->default_target;

      if (default_target != nullptr)
        this->addEdge(block, default_target);

      continue;
    }
    case cfg::Terminator::Tag::VariantSwitchTerm: {
      for (cfg::VariantSwitchClause &clause : term->toVariantSwitchTerm()->clauses)
        this->addEdge(block, clause.target);

      cfg::BasicBlock *default_target = term->toVariantSwitchTerm()->default_target;

      if (default_target != nullptr)
        this->addEdge(block, default_target);

      continue;
    }
    case cfg::Terminator::Tag::ReturnTerm:
    case cfg::Terminator::Tag::PanicTerm:
    case cfg::Terminator::Tag::CallTerm: {
      continue;
    }
    }

    assert(false);
  }
}

void cfg::CFG::addEdge(cfg::BasicBlock *from_bb, cfg::BasicBlock *to_bb) {
  if (std::find(from_bb->successors.begin(), from_bb->successors.end(), to_bb) == from_bb->successors.end())
    from_bb->successors.push_back(to_bb);

  if (std::find(to_bb->predecessors.begin(), to_bb->predecessors.end(), from_bb) == to_bb->predecessors.end())
    to_bb->predecessors.push_back(from_bb);
}

std::string cfg::unaryOpToString(UnaryOp op) {
  switch (op) {
  case UnaryOp::Minus:  return "-";
  case UnaryOp::Not:    return "!";
  case UnaryOp::BitNot: return "~";
  }

  assert(false);
}

std::string cfg::binaryOpToString(BinaryOp op) {
  switch (op) {
  case cfg::BinaryOp::Add:    return "+";
  case cfg::BinaryOp::Sub:    return "-";
  case cfg::BinaryOp::Mul:    return "*";
  case cfg::BinaryOp::Div:    return "/";
  case cfg::BinaryOp::Rem:    return "%";
  case cfg::BinaryOp::LT:     return "<";
  case cfg::BinaryOp::LE:     return "<=";
  case cfg::BinaryOp::GT:     return ">";
  case cfg::BinaryOp::GE:     return ">=";
  case cfg::BinaryOp::EQ:     return "==";
  case cfg::BinaryOp::NE:     return "!=";
  case cfg::BinaryOp::BitAnd: return "&";
  case cfg::BinaryOp::BitOr:  return "|";
  case cfg::BinaryOp::BitXor: return "^";
  case cfg::BinaryOp::SHL:    return "<<";
  case cfg::BinaryOp::SHR:    return ">>";
  }

  assert(false);
}

const Type *cfg::Storage::getType() const {
  switch (this->getTag()) {
  case Tag::Variable:
    return this->get<Variable *>()->type;
  case Tag::Parameter:
    return this->get<Parameter *>()->type;
  case Tag::Temporary:
    return this->get<Temporary *>()->type;
  }

  assert(false);
}

std::string cfg::Storage::toString() const {
  switch (this->getTag()) {
  case Tag::Variable:
    return this->get<Variable *>()->name;
  case Tag::Parameter: {
    const Parameter *param = this->get<Parameter *>();
    // if is out parameter, return "out <...>"
    if (param->isNamed())
      return param->getName();
    else
      return "<unnamed-param>";
  }
  case Tag::Temporary: {
    return std::string("_t") + std::to_string(this->get<Temporary *>()->id);
  }
  }

  assert(false);
}

// Place

const Type *cfg::Place::getType() const {
  const Type *result = this->base.getType();

  for (size_t i = 0, n = this->projections.size(); i < n; ++i) {
    const Projection &proj = this->projections[i];

    switch (proj.getTag()) {
    case Projection::Tag::StructField: {
      result = proj.get<StructField *>()->type;
      continue;
    }
    case Projection::Tag::VariantAlternative: {
      result = proj.get<VariantAlternative *>()->type;
      continue;
    }
    case Projection::Tag::Index: {
      result = result->indexResult();
      continue;
    }
    }

    assert(false);
  }

  return result;
}

const Type *cfg::PlaceBase::getType() const {
  switch (this->getTag()) {
  case Tag::Variable:
    return this->get<Variable *>()->type;
  case Tag::Parameter:
    return this->get<Parameter *>()->type;
  case Tag::Temporary:
    return this->get<cfg::Temporary *>()->type;
  case Tag::Deref:
    return this->get<cfg::Deref>().ptr->getType()->toPointerType()->getPointee();
  }

  assert(false);
}

const Type *cfg::Value::getType() const {
  switch (this->tag) {
  case cfg::Value::Tag::RegisterValue:
    return this->toRegisterValue()->reg->type;
  case cfg::Value::Tag::ConstantValue:
    return this->toConstantValue()->type;
  }

  assert(false);
}
