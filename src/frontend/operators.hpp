#pragma once

#include <string>
#include <variant>

enum class UnaryOp {
  Plus,         // +
  Minus,        // -
  Indirection,  // *
  AddressOf,    // &
  Not,          // !
  BitNot        // ~
};

enum class BinaryOp {
  Add,     // +
  Sub,     // -
  Mul,     // *
  Div,     // /
  Rem,     // %
  LT,      // <
  LE,      // <=
  GT,      // >
  GE,      // >=
  EQ,      // ==
  NE,      // !=
  And,     // &&
  Or,      // ||
  BitAnd,  // &
  BitOr,   // |
  BitXor,  // ^
  SHL,     // <<
  SHR      // >>
};

enum class AssignmentOp {
  Assign,      // =
  Initialize   // :=
};

std::string unaryOpToString(UnaryOp op);
std::string binaryOpToString(BinaryOp op);
std::string assignmentOpToString(AssignmentOp op);

// Listed from lower to higher precedence so that comparison works.
enum class Precedence {
  Assignment,     // =, :=
  Or,             // ||
  And,            // &&
  Relational,     // <, <=, >, >=, ==, !=
  BitOr,          // |
  BitXor,         // ^
  BitAnd,         // &
  Shift,          // <<, >>
  Additive,       // +, -
  Multiplicative  // *, /, %
};

using InfixOp = std::variant<BinaryOp, AssignmentOp>;

Precedence getPrecedence(BinaryOp op);
Precedence getPrecedence(AssignmentOp op);
Precedence getPrecedence(InfixOp op);

bool isLeftAssociative(Precedence prec);
