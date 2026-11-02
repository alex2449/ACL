#pragma once

#include <cassert>
#include <optional>
#include <string>
#include <variant>

#include "gmpxx.h"
#include "operators.hpp"
#include "source-location.hpp"
#include "../util/util.hpp"

enum class Keyword {
  As,
  AsExclaim,
  Bool,
  Break,
  Case,
  Char,
  Constant,
  Continue,
  Default,
  Else,
  Enum,
  Extern,
  Fallthrough,
  False,
  For,
  Function,
  If,
  Import,
  Int,
  Int16,
  Int32,
  Int64,
  Int8,
  Is,
  New,
  Not,
  Null,
  Return,
  Returns,
  Skip,
  Str,
  String,
  Struct,
  Switch,
  True,
  Type,
  UInt,
  UInt16,
  UInt32,
  UInt64,
  UInt8,
  Var,
  Variant,
  While
};

std::string keywordToString(Keyword keyword);

enum class Punctuator {
  Lsquare,         // [
  Rsquare,         // ]
  Lparen,          // (
  Rparen,          // )
  Lcurly,          // {
  Rcurly,          // }
  Period,          // .
  Arrow,           // ->
  Amp,             // &
  Asterisk,        // *
  Plus,            // +
  Minus,           // -
  Tilde,           // ~
  Exclaim,         // !
  Slash,           // /
  Percent,         // %
  LessLess,        // <<
  GreaterGreater,  // >>
  Less,            // <
  Greater,         // >
  LessEqual,       // <=
  GreaterEqual,    // >=
  EqualEqual,      // ==
  ExclaimEqual,    // !=
  Caret,           // ^
  Pipe,            // |
  AmpAmp,          // &&
  PipePipe,        // ||
  Colon,           // :
  Semicolon,       // ;
  Equal,           // =
  ColonEqual,      // :=
  Comma            // ,
};

struct TokenBase {
  SourceRange range;
  TokenBase(SourceRange range) : range(range) { }
};

struct KeywordToken : public TokenBase {
  Keyword value;
  KeywordToken(SourceRange range, Keyword value) : TokenBase(range), value(value) { }
};

struct IdToken : public TokenBase {
  std::string value;
  IdToken(SourceRange range, std::string value) : TokenBase(range), value(std::move(value)) { }
};

struct IntToken : public TokenBase {
  mpz_class value;
  IntToken(SourceRange range, mpz_class value) : TokenBase(range), value(std::move(value)) { }
};

struct PunctuatorToken : public TokenBase {
  Punctuator value;
  PunctuatorToken(SourceRange range, Punctuator value) : TokenBase(range), value(value) { }
};

struct StringToken : public TokenBase {
  std::string value;
  StringToken(SourceRange range, std::string value) : TokenBase(range), value(std::move(value)) { }
};

struct CharToken : public TokenBase {
  char value;
  CharToken(SourceRange range, char value) : TokenBase(range), value(value) { }
};

struct EOFToken : public TokenBase {
  EOFToken(SourceRange range) : TokenBase(range) { }
};

struct ErrorToken : public TokenBase {
  ErrorToken(SourceRange range) : TokenBase(range) { }
};

struct Token : public variant<Token, KeywordToken, IdToken, IntToken, PunctuatorToken, StringToken, CharToken, EOFToken, ErrorToken> {
 public:
  enum class Tag { KeywordToken, IdToken, IntToken, PunctuatorToken, StringToken, CharToken, EOFToken, ErrorToken };
  using variant::variant;

  SourceRange &getSourceRange() { return this->visit([](TokenBase &tok) -> SourceRange & { return tok.range; }); }
  const SourceRange &getSourceRange() const { return this->visit([](const TokenBase &tok) -> const SourceRange & { return tok.range; }); }

  Keyword getKeywordValue() const { return this->get<KeywordToken>().value; }
  std::string &getIdValue() { return this->get<IdToken>().value; }
  mpz_class &getIntValue() { return this->get<IntToken>().value; }
  std::string &getStringValue() { return this->get<StringToken>().value; }
  char getCharValue() const { return this->get<CharToken>().value; }

  bool isKeyword(Keyword value) const {
    return this->getTag() == Tag::KeywordToken && this->get<KeywordToken>().value == value;
  }

  bool isPunctuator(Punctuator value) const {
    return this->getTag() == Tag::PunctuatorToken && this->get<PunctuatorToken>().value == value;
  }

  bool isElementaryTypeName() const;

  std::optional<UnaryOp> toUnaryOp() const;
  std::optional<BinaryOp> toBinaryOp() const;
  std::optional<AssignmentOp> toAssignmentOp() const;
  std::optional<InfixOp> toInfixOp() const;

  void dump() const;
};
