#include "llc.hpp"

#include <cassert>
#include <variant>

llc::RegisterValue *llc::Variable::asValue() {
  return new llc::RegisterValue(this->type, this);
}

std::vector<llc::Type *> llc::Type::dependencies() const {
  auto f = [](const std::vector<llc::Field *> &fields) {
    std::vector<llc::Type *> dependencies;

    for (const llc::Field *field : fields) {
      if (field->type->isStructType() || field->type->isUnionType() || field->type->isEnumType())
        dependencies.push_back(field->type);

      if (field->type->isArrayType()
          && (field->type->toArrayType()->element->isStructType()
              || field->type->toArrayType()->element->isUnionType()
              || field->type->toArrayType()->element->isEnumType()))
        dependencies.push_back(field->type->toArrayType()->element);
    }

    return dependencies;
  };

  switch (this->tag) {
  case llc::Type::Tag::StructType:
    return f(this->toStructType()->fields);
  case llc::Type::Tag::UnionType:
    return f(this->toUnionType()->fields);
  default:
    return {};
  }
}

const llc::Type *llc::PlaceBase::getType() const {
  switch (this->getTag()) {
  case Tag::Variable:
    return this->get<Variable *>()->type;
  case Tag::Parameter:
    return this->get<Parameter *>()->type;
  case Tag::Deref:
    return this->get<Deref>().ptr->type->toPointerType()->pointee;
  }

  assert(false);
}

const llc::Type *llc::Place::getType() const {
  const llc::Type *result = this->base.getType();

  for (size_t i = 0, n = this->projections.size(); i < n; ++i) {
    const llc::Projection &proj = this->projections[i];

    switch (proj.getTag()) {
    case llc::Projection::Tag::Field: {
      result = proj.get<llc::Field *>()->type;
      continue;
    }
    case llc::Projection::Tag::Index: {
      result = result->toArrayType()->element;
      continue;
    }
    }

    assert(false);
  }

  return result;
}

std::string llc::unaryOpToString(UnaryOp op) {
  switch (op) {
  case llc::UnaryOp::Minus:       return "-";
  case llc::UnaryOp::Not:         return "!";
  case llc::UnaryOp::BitNot:      return "~";
  }

  assert(false);
}

std::string llc::binaryOpToString(BinaryOp op) {
  switch (op) {
  case llc::BinaryOp::Add:    return "+";
  case llc::BinaryOp::Sub:    return "-";
  case llc::BinaryOp::Mul:    return "*";
  case llc::BinaryOp::Div:    return "/";
  case llc::BinaryOp::Rem:    return "%";
  case llc::BinaryOp::LT:     return "<";
  case llc::BinaryOp::LE:     return "<=";
  case llc::BinaryOp::GT:     return ">";
  case llc::BinaryOp::GE:     return ">=";
  case llc::BinaryOp::EQ:     return "==";
  case llc::BinaryOp::NE:     return "!=";
  case llc::BinaryOp::BitAnd: return "&";
  case llc::BinaryOp::BitOr:  return "|";
  case llc::BinaryOp::BitXor: return "^";
  case llc::BinaryOp::SHL:    return "<<";
  case llc::BinaryOp::SHR:    return ">>";
  }

  assert(false);
}
