#include "lexer.hpp"

#include <cassert>
#include <string>
#include <unordered_map>
#include <utility>

#include "diagnostics.hpp"
#include "fmt/format.h"
#include "source-location.hpp"
#include "source.hpp"
#include "token.hpp"
#include "../util/util.hpp"

bool Lexer::isCommon(char c) {
  return isAsciiPrintable(c) && c != '\'' && c != '\"' && c != '\\';
}

bool Lexer::isWhiteSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool Lexer::isLetter(char c) {
  return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z');
}

bool Lexer::isBinaryDigit(char c) {
  return c == '0' || c == '1';
}

bool Lexer::isDecimalDigit(char c) {
  return '0' <= c && c <= '9';
}

bool Lexer::isHexDigit(char c) {
  return Lexer::isDecimalDigit(c) || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f');
}

int Lexer::binaryDigitToInt(char c) {
  assert(Lexer::isBinaryDigit(c) && "c is not a binary digit");
  return c - '0';
}

int Lexer::decimalDigitToInt(char c) {
  assert(Lexer::isDecimalDigit(c) && "c is not a decimal digit");
  return c - '0';
}

int Lexer::hexDigitToInt(char c) {
  if ('0' <= c && c <= '9') return c - '0';
  if ('A' <= c && c <= 'F') return c - 'A' + 10;
  if ('a' <= c && c <= 'f') return c - 'a' + 10;
  assert(false && "c is not a hex digit");
}

static const std::unordered_map<std::string, Keyword> keywords_map = {
  { "as",          Keyword::As          },
  { "as!",         Keyword::AsExclaim   },
  { "bool",        Keyword::Bool        },
  { "break",       Keyword::Break       },
  { "case",        Keyword::Case        },
  { "char",        Keyword::Char        },
  { "constant",    Keyword::Constant    },
  { "continue",    Keyword::Continue    },
  { "default",     Keyword::Default     },
  { "else",        Keyword::Else        },
  { "enum",        Keyword::Enum        },
  { "extern",      Keyword::Extern      },
  { "fallthrough", Keyword::Fallthrough },
  { "false",       Keyword::False       },
  { "for",         Keyword::For         },
  { "function",    Keyword::Function    },
  { "if",          Keyword::If          },
  { "import",      Keyword::Import      },
  { "int",         Keyword::Int         },
  { "int16",       Keyword::Int16       },
  { "int32",       Keyword::Int32       },
  { "int64",       Keyword::Int64       },
  { "int8",        Keyword::Int8        },
  { "is",          Keyword::Is          },
  { "new",         Keyword::New         },
  { "not",         Keyword::Not         },
  { "null",        Keyword::Null        },
  { "return",      Keyword::Return      },
  { "returns",     Keyword::Returns     },
  { "skip",        Keyword::Skip        },
  { "str",         Keyword::Str         },
  { "string",      Keyword::String      },
  { "struct",      Keyword::Struct      },
  { "switch",      Keyword::Switch      },
  { "true",        Keyword::True        },
  { "type",        Keyword::Type        },
  { "uint",        Keyword::UInt        },
  { "uint16",      Keyword::UInt16      },
  { "uint32",      Keyword::UInt32      },
  { "uint64",      Keyword::UInt64      },
  { "uint8",       Keyword::UInt8       },
  { "var",         Keyword::Var         },
  { "variant",     Keyword::Variant     },
  { "while",       Keyword::While       }
};

void Lexer::lexSourceAndDumpTokens(bool print_source) {
  while (true) {
    Token token = this->lexNextToken();
    token.dump();

    if (print_source)
      this->diag->note(token.getSourceRange(), "token is here");

    if (token.is<EOFToken>())
      break;
  }
}

char Lexer::currentChar() const {
  return this->text.data()[this->offset];
}

char Lexer::peekChar() const {
  // Never look past the sentinel.
  if (this->offset >= this->text.size())
    return '\0';
  return this->text.data()[this->offset + 1];
}

char Lexer::advanceChar() {
  // Stay on the EOF offset once we reach it.
  if (this->offset < this->text.size())
    ++this->offset;
  return this->currentChar();
}

Token Lexer::lexNextToken() {
  assert(!this->saw_eof && "nothing left to lex");

  // Skip whitespace and comments.
  while (true) {
    char c = this->currentChar();

    if (Lexer::isWhiteSpace(c)) {
      this->advanceChar();
    } else if (c == '/' && this->peekChar() == '/') {
      // Skip single-line comment.
      while (true) {
        c = this->advanceChar();
        if (c == '\n' || c == '\0')
          break;
      }
    } else if (c == '/' && this->peekChar() == '*') {
      this->skipMultiLineComment();
    } else {
      break;
    }
  }

  char c = this->currentChar();

  if (Lexer::isLetter(c)) {
    uint32_t begin = this->offset;

    std::string value;
    do {
      value += c;
      c = this->advanceChar();
    } while (Lexer::isLetter(c) || Lexer::isDecimalDigit(c) || c == '_');

    if (value == "as" && c == '!') {
      value.push_back('!');
      this->advanceChar();
    }

    SourceRange range = this->sourceRange(begin, this->offset - 1);

    std::unordered_map<std::string, Keyword>::const_iterator it = keywords_map.find(value);
    if (it != keywords_map.end())
      return KeywordToken(range, it->second);

    return IdToken(range, std::move(value));
  }

  if (c == '0' && (this->peekChar() == 'b' || this->peekChar() == 'x')) {
    uint32_t begin = this->offset;

    int base = this->peekChar() == 'b' ? 2 : 16;

    this->advanceChar();
    this->advanceChar();

    bool saw_digit = false;
    mpz_class value = 0;
    while (true) {
      char c = this->currentChar();

      if (base == 2 && !Lexer::isBinaryDigit(c) && c != '_')
        break;

      if (base == 16 && !Lexer::isHexDigit(c) && c != '_')
        break;

      if (c == '_') {
        this->advanceChar();
        continue;
      }

      if (!saw_digit)
        saw_digit = true;

      if (base == 2)
        value = value * 2 + binaryDigitToInt(c);
      else
        value = value * 16 + hexDigitToInt(c);

      this->advanceChar();
    }

    SourceRange range = this->sourceRange(begin, this->offset - 1);

    if (!saw_digit) {
      this->diag->error(range, "invalid numeric literal");
      return ErrorToken(range);
    }

    return IntToken(range, std::move(value));
  }

  if (Lexer::isDecimalDigit(c)) {
    uint32_t begin = this->offset;

    mpz_class value = 0;
    do {
      value = value * 10 + decimalDigitToInt(c);
      c = this->advanceChar();
    } while (Lexer::isDecimalDigit(c));

    SourceRange range = this->sourceRange(begin, this->offset - 1);
    return IntToken(range, std::move(value));
  }

  if (c == '\'' || c == '\"') {
    // We lex character literals and string literals similarly.
    // For character literals, we check if exactly one character was lexed.
    uint32_t begin = this->offset;
    uint32_t end;

    bool saw_error = false;

    std::string value = this->lexQuotedLiteral(&saw_error, &end);

    SourceRange range = this->sourceRange(begin, end);

    // Check for empty or multi-character character literals.
    // Only report an error if an error was not already reported.
    if (c == '\'' && !saw_error) {
      if (value.empty()) {
        saw_error = true;
        this->diag->error(range, "empty character literal");
      }

      if (value.size() > 1) {
        saw_error = true;
        this->diag->error(range, "multi-character character literal");
      }
    }

    if (saw_error)
      return ErrorToken(range);

    return (c == '\''
            ? Token(CharToken(range, value[0]))
            : Token(StringToken(range, std::move(value))));
  }

  switch (c) {
  case '[':
    return this->consumeOneCharacterPunctuator(Punctuator::Lsquare);
  case ']':
    return this->consumeOneCharacterPunctuator(Punctuator::Rsquare);
  case '(':
    return this->consumeOneCharacterPunctuator(Punctuator::Lparen);
  case ')':
    return this->consumeOneCharacterPunctuator(Punctuator::Rparen);
  case '{':
    return this->consumeOneCharacterPunctuator(Punctuator::Lcurly);
  case '}':
    return this->consumeOneCharacterPunctuator(Punctuator::Rcurly);
  case '.':
    return this->consumeOneCharacterPunctuator(Punctuator::Period);
  case '-':
    if (this->peekChar() == '>')
      return this->consumeTwoCharacterPunctuator(Punctuator::Arrow);
    return this->consumeOneCharacterPunctuator(Punctuator::Minus);
  case '&':
    if (this->peekChar() == '&')
      return this->consumeTwoCharacterPunctuator(Punctuator::AmpAmp);
    return this->consumeOneCharacterPunctuator(Punctuator::Amp);
  case '*':
    return this->consumeOneCharacterPunctuator(Punctuator::Asterisk);
  case '+':
    return this->consumeOneCharacterPunctuator(Punctuator::Plus);
  case '~':
    return this->consumeOneCharacterPunctuator(Punctuator::Tilde);
  case '!':
    if (this->peekChar() == '=')
      return this->consumeTwoCharacterPunctuator(Punctuator::ExclaimEqual);
    return this->consumeOneCharacterPunctuator(Punctuator::Exclaim);
  case '/':
    return this->consumeOneCharacterPunctuator(Punctuator::Slash);
  case '%':
    return this->consumeOneCharacterPunctuator(Punctuator::Percent);
  case '<':
    if (this->peekChar() == '=')
      return this->consumeTwoCharacterPunctuator(Punctuator::LessEqual);
    if (this->peekChar() == '<')
      return this->consumeTwoCharacterPunctuator(Punctuator::LessLess);
    return this->consumeOneCharacterPunctuator(Punctuator::Less);
  case '>':
    if (this->peekChar() == '=')
      return this->consumeTwoCharacterPunctuator(Punctuator::GreaterEqual);
    if (this->peekChar() == '>')
      return this->consumeTwoCharacterPunctuator(Punctuator::GreaterGreater);
    return this->consumeOneCharacterPunctuator(Punctuator::Greater);
  case '=':
    if (this->peekChar() == '=')
      return this->consumeTwoCharacterPunctuator(Punctuator::EqualEqual);
    return this->consumeOneCharacterPunctuator(Punctuator::Equal);
  case '^':
    return this->consumeOneCharacterPunctuator(Punctuator::Caret);
  case '|':
    if (this->peekChar() == '|')
      return this->consumeTwoCharacterPunctuator(Punctuator::PipePipe);
    return this->consumeOneCharacterPunctuator(Punctuator::Pipe);
  case ':':
    if (this->peekChar() == '=')
      return this->consumeTwoCharacterPunctuator(Punctuator::ColonEqual);
    return this->consumeOneCharacterPunctuator(Punctuator::Colon);
  case ';':
    return this->consumeOneCharacterPunctuator(Punctuator::Semicolon);
  case ',':
    return this->consumeOneCharacterPunctuator(Punctuator::Comma);
  }

  if (c == '\0') {
    this->saw_eof = true;
    SourceRange range = this->sourceRange(this->offset, this->offset);
    return EOFToken(range);
  }

  SourceLocation location = this->sourceLocation(this->offset);
  this->diag->error(location, "can't match character '{}'", c);

  SourceRange range = this->sourceRange(this->offset, this->offset);
  this->advanceChar();

  return ErrorToken(range);
}

void Lexer::skipMultiLineComment() {
  assert(this->currentChar() == '/');

  unsigned hold_offset = this->offset;

  this->advanceChar();
  assert(this->currentChar() == '*');

  this->advanceChar();

  while (true) {
    char c = this->currentChar();

    if (c == '*' && this->peekChar() == '/') {
      // Consume both '*' and '/'.
      this->advanceChar();
      this->advanceChar();
      return;
    } else if (c == '/' && this->peekChar() == '*') {
      // Multi-line comments can be nested.
      this->skipMultiLineComment();
    } else if (c == '\0') {
      SourceLocation location = this->sourceLocation(hold_offset);
      this->diag->error(location, "unterminated comment");
      return;
    } else {
      this->advanceChar();
    }
  }
}

// Lex a sequence of zero or more common characters or escape sequences within single or double quotes.

std::string Lexer::lexQuotedLiteral(bool *saw_error, uint32_t *end) {
  char quotes = this->currentChar();
  assert(quotes == '\'' || quotes == '\"');
  char other = (quotes == '\"' ? '\'' : '\"');

  uint32_t begin = this->offset;

  std::string value;

  this->advanceChar();

  while (true) {
    char c = this->currentChar();

    if (c == '\n' || c == '\0') {
      SourceRange range = this->sourceRange(begin, this->offset - 1);
      this->diag->error(range, "unterminated {} literal", quotes == '\'' ? "character" : "string");
      *saw_error = true;
      *end = this->offset - 1;
      break;
    } else if (c == quotes) {
      *end = this->offset;
      this->advanceChar();
      break;
    } else if (Lexer::isCommon(c) || c == other) {
      value += c;
      this->advanceChar();
    } else if (c == '\\') {
      this->lexEscapeSequence(&value, saw_error);
    } else {
      if (!*saw_error) {
        *saw_error = true;
        SourceLocation location = this->sourceLocation(this->offset);
        if (c == '\t') {
          this->diag->error(location, "tabs are not allowed within {} literals, use the '\\t' "
                            "escape sequence instead", quotes == '\'' ? "character" : "string");
        } else {
          this->diag->error(location, "expected common character or escape "
                            "sequence (found value {})", charValueToLiteral(c));
        }
      }

      this->advanceChar();
    }
  }

  return value;
}

void Lexer::lexEscapeSequence(std::string *value, bool *saw_error) {
  assert(this->currentChar() == '\\');

  uint32_t begin = this->offset;

  char c = this->advanceChar();
  switch (c) {
  case 'n':
    *value += '\n';
    this->advanceChar();
    return;
  case 't':
    *value += '\t';
    this->advanceChar();
    return;
  case 'r':
    *value += '\r';
    this->advanceChar();
    return;
  case '0':
    *value += '\0';
    this->advanceChar();
    return;
  case '\\':
    *value += '\\';
    this->advanceChar();
    return;
  case '\'':
    *value += '\'';
    this->advanceChar();
    return;
  case '\"':
    *value += '\"';
    this->advanceChar();
    return;
  case 'x': {
    c = this->advanceChar();
    if (!Lexer::isHexDigit(c))
      break;
    int high = Lexer::hexDigitToInt(c);

    c = this->advanceChar();
    if (!Lexer::isHexDigit(c))
      break;
    int low = Lexer::hexDigitToInt(c);

    *value += (high << 4) | low;
    this->advanceChar();
    return;
  }
  default:
    break;
  }

  if (!*saw_error) {
    *saw_error = true;
    unsigned end = (c == '\n' || c == '\0') ? this->offset - 1 : this->offset;
    SourceRange range = this->sourceRange(begin, end);
    this->diag->error(range, "invalid escape sequence");
  }
}

Token Lexer::consumeOneCharacterPunctuator(Punctuator punctuator) {
  SourceRange range = this->sourceRange(this->offset, this->offset);
  this->advanceChar();
  return PunctuatorToken(range, punctuator);
}

Token Lexer::consumeTwoCharacterPunctuator(Punctuator punctuator) {
  SourceRange range = this->sourceRange(this->offset, this->offset + 1);
  this->advanceChar();
  this->advanceChar();
  return PunctuatorToken(range, punctuator);
}
