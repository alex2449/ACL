#pragma once

#include "token.hpp"
#include "source.hpp"

class Diagnostics;

class Lexer {
 public:
  Lexer(FileID file, const SourceManager *src_mger, Diagnostics *diag)
      : file(file), src_mger(src_mger), text(src_mger->getText(file)), diag(diag) { }

  void lexSourceAndDumpTokens(bool print_source);

  Token lexNextToken();

 private:
  static bool isCommon(char c);
  static bool isWhiteSpace(char c);
  static bool isLetter(char c);
  static bool isBinaryDigit(char c);
  static bool isDecimalDigit(char c);
  static bool isHexDigit(char c);

  static int binaryDigitToInt(char c);
  static int decimalDigitToInt(char c);
  static int hexDigitToInt(char c);

  char currentChar() const;
  char peekChar() const;
  char advanceChar();

  SourceLocation sourceLocation(uint32_t offset) {
    return this->src_mger->getLocation(this->file, offset);
  }

  SourceRange sourceRange(uint32_t begin, uint32_t end) {
    return this->src_mger->getRange(this->file, begin, end);
  }

  void skipMultiLineComment();
  std::string lexQuotedLiteral(bool *saw_error, uint32_t *end);
  void lexEscapeSequence(std::string *value, bool *saw_error);

  Token consumeOneCharacterPunctuator(Punctuator punctuator);
  Token consumeTwoCharacterPunctuator(Punctuator punctuator);

  // The source we are lexing.
  FileID file;
  const SourceManager *src_mger;
  std::string_view text;
  Diagnostics *diag;
  // The current offset in the file.
  uint32_t offset = 0;
  bool saw_eof = false;
};
