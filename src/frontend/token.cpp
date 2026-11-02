#include "token.hpp"

#include <iostream>

#include "operators.hpp"
#include "../util/util.hpp"

static std::string punctuatorToString(Punctuator punctuator);

void Token::dump() const {
  switch (this->getTag()) {
  case Tag::KeywordToken:
    std::cout << "KeywordToken" << " " << this->getSourceRange().toReadableString()
              << " " << keywordToString(this->get<KeywordToken>().value) << "\n";
    return;
  case Tag::PunctuatorToken:
    std::cout << "PunctuatorToken" << " " << this->getSourceRange().toReadableString()
              << " " << punctuatorToString(this->get<PunctuatorToken>().value) << "\n";
    return;
  case Tag::IdToken:
    std::cout << "IdToken" << " " << this->getSourceRange().toReadableString()
              << " " << this->get<IdToken>().value << "\n";
    return;
  case Tag::IntToken:
    std::cout << "IntToken" << " " << this->getSourceRange().toReadableString()
              << " " << this->get<IntToken>().value << "\n";
    return;
  case Tag::CharToken:
    std::cout << "CharToken" << " " << this->getSourceRange().toReadableString()
              << " " << charValueToLiteral(this->get<CharToken>().value) << "\n";
    return;
  case Tag::StringToken:
    std::cout << "StringToken" << " " << this->getSourceRange().toReadableString()
              << " " << stringValueToLiteral(this->get<StringToken>().value) << "\n";
    return;
  case Tag::EOFToken:
    std::cout << "EOFToken" << " " << this->getSourceRange().toReadableString() << "\n";
    return;
  case Tag::ErrorToken:
    std::cout << "ErrorToken" << " " << this->getSourceRange().toReadableString() << "\n";
    return;
  }

  assert(false);
}

std::string keywordToString(Keyword keyword) {
  switch (keyword) {
  case Keyword::As:          return "as";
  case Keyword::AsExclaim:   return "as!";
  case Keyword::Bool:        return "bool";
  case Keyword::Break:       return "break";
  case Keyword::Case:        return "case";
  case Keyword::Char:        return "char";
  case Keyword::Constant:    return "constant";
  case Keyword::Continue:    return "continue";
  case Keyword::Default:     return "default";
  case Keyword::Else:        return "else";
  case Keyword::Enum:        return "enum";
  case Keyword::Extern:      return "extern";
  case Keyword::Fallthrough: return "fallthrough";
  case Keyword::False:       return "false";
  case Keyword::For:         return "for";
  case Keyword::Function:    return "function";
  case Keyword::If:          return "if";
  case Keyword::Import:      return "import";
  case Keyword::Int:         return "int";
  case Keyword::Int16:       return "int16";
  case Keyword::Int32:       return "int32";
  case Keyword::Int64:       return "int64";
  case Keyword::Int8:        return "int8";
  case Keyword::Is:          return "is";
  case Keyword::New:         return "new";
  case Keyword::Not:         return "not";
  case Keyword::Null:        return "null";
  case Keyword::Return:      return "return";
  case Keyword::Returns:     return "returns";
  case Keyword::Skip:        return "skip";
  case Keyword::Str:         return "str";
  case Keyword::String:      return "string";
  case Keyword::Struct:      return "struct";
  case Keyword::Switch:      return "switch";
  case Keyword::True:        return "true";
  case Keyword::Type:        return "type";
  case Keyword::UInt:        return "uint";
  case Keyword::UInt16:      return "uint16";
  case Keyword::UInt32:      return "uint32";
  case Keyword::UInt64:      return "uint64";
  case Keyword::UInt8:       return "uint8";
  case Keyword::Var:         return "var";
  case Keyword::Variant:     return "variant";
  case Keyword::While:       return "while";
  }

  assert(false);
}

std::string punctuatorToString(Punctuator punctuator) {
  switch (punctuator) {
  case Punctuator::Lsquare:        return "[";
  case Punctuator::Rsquare:        return "]";
  case Punctuator::Lparen:         return "(";
  case Punctuator::Rparen:         return ")";
  case Punctuator::Lcurly:         return "{";
  case Punctuator::Rcurly:         return "}";
  case Punctuator::Period:         return ".";
  case Punctuator::Arrow:          return "->";
  case Punctuator::Amp:            return "&";
  case Punctuator::Asterisk:       return "*";
  case Punctuator::Plus:           return "+";
  case Punctuator::Minus:          return "-";
  case Punctuator::Tilde:          return "~";
  case Punctuator::Exclaim:        return "!";
  case Punctuator::Slash:          return "/";
  case Punctuator::Percent:        return "%";
  case Punctuator::LessLess:       return "<<";
  case Punctuator::GreaterGreater: return ">>";
  case Punctuator::Less:           return "<";
  case Punctuator::Greater:        return ">";
  case Punctuator::LessEqual:      return "<=";
  case Punctuator::GreaterEqual:   return ">=";
  case Punctuator::EqualEqual:     return "==";
  case Punctuator::ExclaimEqual:   return "!=";
  case Punctuator::Caret:          return "^";
  case Punctuator::Pipe:           return "|";
  case Punctuator::AmpAmp:         return "&&";
  case Punctuator::PipePipe:       return "||";
  case Punctuator::Colon:          return ":";
  case Punctuator::Semicolon:      return ";";
  case Punctuator::Equal:          return "=";
  case Punctuator::ColonEqual:     return ":=";
  case Punctuator::Comma:          return ",";
  }

  assert(false);
}

bool Token::isElementaryTypeName() const {
  if (this->getTag() != Tag::KeywordToken)
    return false;

  Keyword keyword = this->getKeywordValue();

  return (keyword == Keyword::Bool
          || keyword == Keyword::Char
          || keyword == Keyword::Int
          || keyword == Keyword::Int8
          || keyword == Keyword::Int16
          || keyword == Keyword::Int32
          || keyword == Keyword::Int64
          || keyword == Keyword::UInt
          || keyword == Keyword::UInt8
          || keyword == Keyword::UInt16
          || keyword == Keyword::UInt32
          || keyword == Keyword::UInt64
          || keyword == Keyword::Str
          || keyword == Keyword::String);
}

std::optional<UnaryOp> Token::toUnaryOp() const {
  if (const PunctuatorToken *tok = this->get_if<PunctuatorToken>()) {
    switch (tok->value) {
    case Punctuator::Plus:     return UnaryOp::Plus;
    case Punctuator::Minus:    return UnaryOp::Minus;
    case Punctuator::Asterisk: return UnaryOp::Indirection;
    case Punctuator::Amp:      return UnaryOp::AddressOf;
    case Punctuator::Exclaim:  return UnaryOp::Not;
    case Punctuator::Tilde:    return UnaryOp::BitNot;
    default:
      break;
    }
  }

  return std::nullopt;
}

std::optional<BinaryOp> Token::toBinaryOp() const {
  if (const PunctuatorToken *tok = this->get_if<PunctuatorToken>()) {
    switch (tok->value) {
    case Punctuator::Plus:           return BinaryOp::Add;
    case Punctuator::Minus:          return BinaryOp::Sub;
    case Punctuator::Asterisk:       return BinaryOp::Mul;
    case Punctuator::Slash:          return BinaryOp::Div;
    case Punctuator::Percent:        return BinaryOp::Rem;
    case Punctuator::Less:           return BinaryOp::LT;
    case Punctuator::LessEqual:      return BinaryOp::LE;
    case Punctuator::Greater:        return BinaryOp::GT;
    case Punctuator::GreaterEqual:   return BinaryOp::GE;
    case Punctuator::EqualEqual:     return BinaryOp::EQ;
    case Punctuator::ExclaimEqual:   return BinaryOp::NE;
    case Punctuator::AmpAmp:         return BinaryOp::And;
    case Punctuator::PipePipe:       return BinaryOp::Or;
    case Punctuator::Amp:            return BinaryOp::BitAnd;
    case Punctuator::Pipe:           return BinaryOp::BitOr;
    case Punctuator::Caret:          return BinaryOp::BitXor;
    case Punctuator::LessLess:       return BinaryOp::SHL;
    case Punctuator::GreaterGreater: return BinaryOp::SHR;
    default:
      break;
    }
  }

  return std::nullopt;
}

std::optional<AssignmentOp> Token::toAssignmentOp() const {
  if (const PunctuatorToken *tok = this->get_if<PunctuatorToken>()) {
    switch (tok->value) {
    case Punctuator::Equal:      return AssignmentOp::Assign;
    case Punctuator::ColonEqual: return AssignmentOp::Initialize;
    default:
      break;
    }
  }

  return std::nullopt;
}

std::optional<InfixOp> Token::toInfixOp() const {
  std::optional<BinaryOp> bin_op = this->toBinaryOp();
  if (bin_op.has_value())
    return bin_op.value();
  std::optional<AssignmentOp> assignment_op = this->toAssignmentOp();
  if (assignment_op.has_value())
    return assignment_op.value();
  return std::nullopt;
}
