#include "operators.hpp"

#include <cassert>

std::string unaryOpToString(UnaryOp op) {
  switch (op) {
  case UnaryOp::Plus:        return "+";
  case UnaryOp::Minus:       return "-";
  case UnaryOp::Indirection: return "*";
  case UnaryOp::AddressOf:   return "&";
  case UnaryOp::Not:         return "!";
  case UnaryOp::BitNot:      return "~";
  default:
    assert(false && "unknown unary operator");
  }
}

std::string binaryOpToString(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add:    return "+";
  case BinaryOp::Sub:    return "-";
  case BinaryOp::Mul:    return "*";
  case BinaryOp::Div:    return "/";
  case BinaryOp::Rem:    return "%";
  case BinaryOp::LT:     return "<";
  case BinaryOp::LE:     return "<=";
  case BinaryOp::GT:     return ">";
  case BinaryOp::GE:     return ">=";
  case BinaryOp::EQ:     return "==";
  case BinaryOp::NE:     return "!=";
  case BinaryOp::And:    return "&&";
  case BinaryOp::Or:     return "||";
  case BinaryOp::BitAnd: return "&";
  case BinaryOp::BitOr:  return "|";
  case BinaryOp::BitXor: return "^";
  case BinaryOp::SHL:    return "<<";
  case BinaryOp::SHR:    return ">>";
  default:
    assert(false && "unknown binary operator");
  }
}

std::string assignmentOpToString(AssignmentOp op) {
  switch (op) {
  case AssignmentOp::Assign:     return "=";
  case AssignmentOp::Initialize: return ":=";
  default:
    assert(false && "unknown assignment operator");
  }
}

Precedence getPrecedence(BinaryOp op) {
  switch (op) {
  case BinaryOp::Mul:
  case BinaryOp::Div:
  case BinaryOp::Rem:
    return Precedence::Multiplicative;
  case BinaryOp::Add:
  case BinaryOp::Sub:
    return Precedence::Additive;
  case BinaryOp::SHL:
  case BinaryOp::SHR:
    return Precedence::Shift;
  case BinaryOp::BitAnd:
    return Precedence::BitAnd;
  case BinaryOp::BitXor:
    return Precedence::BitXor;
  case BinaryOp::BitOr:
    return Precedence::BitOr;
  case BinaryOp::LT:
  case BinaryOp::LE:
  case BinaryOp::GT:
  case BinaryOp::GE:
  case BinaryOp::EQ:
  case BinaryOp::NE:
    return Precedence::Relational;
  case BinaryOp::And:
    return Precedence::And;
  case BinaryOp::Or:
    return Precedence::Or;
  default:
    assert(false && "unknown binary operator");
  }
}

Precedence getPrecedence(AssignmentOp op) {
  switch (op) {
  case AssignmentOp::Assign:
  case AssignmentOp::Initialize:
    return Precedence::Assignment;
  default:
    assert(false && "unknown assignment operator");
  }
}

Precedence getPrecedence(InfixOp op) {
  if (std::holds_alternative<BinaryOp>(op))
    return getPrecedence(std::get<BinaryOp>(op));
  return getPrecedence(std::get<AssignmentOp>(op));
}

bool isLeftAssociative(Precedence prec) {
  switch (prec) {
  case Precedence::Assignment:
    return false;
  case Precedence::Or:
  case Precedence::And:
  case Precedence::Relational:
  case Precedence::BitOr:
  case Precedence::BitXor:
  case Precedence::BitAnd:
  case Precedence::Shift:
  case Precedence::Additive:
  case Precedence::Multiplicative:
    return true;
  default:
    assert(false && "unknown precedence");
  }
}
