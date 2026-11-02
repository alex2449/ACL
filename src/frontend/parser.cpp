#include "parser.hpp"

#include <cassert>
#include <memory>
#include <variant>

#include "acl.hpp"
#include "diagnostics.hpp"
#include "lexer.hpp"
#include "operators.hpp"
#include "source-location.hpp"
#include "token.hpp"

Token *Parser::advanceToken() {
  if (this->next.has_value()) {
    this->token = std::move(this->next.value());
    this->next.reset();
  } else {
    this->token = this->lexer->lexNextToken();
  }

  return &this->token;
}

Token *Parser::peekToken() {
  if (!this->next.has_value())
    this->next = this->lexer->lexNextToken();

  return &this->next.value();
}

void Parser::error(const std::string &message) const {
  // We currently abort on syntax errors
  this->diag->fatal(this->token.getSourceRange().getBegin(), message);
}

std::unique_ptr<SourceUnit> Parser::parse(std::string path) {
  std::unique_ptr<Scope> scope = std::make_unique<Scope>(nullptr);
  this->scope_stack.push_back(scope.get());

  std::vector<Import> imports = this->parseImports();
  std::vector<SourceItem> items = this->parseSourceItems();

  if (!this->token.is<EOFToken>())
    this->error("expected end of file");

  this->scope_stack.pop_back();
  return std::make_unique<SourceUnit>(std::move(path), std::move(imports), std::move(items), std::move(scope));
}

std::vector<Import> Parser::parseImports() {
  std::vector<Import> imports;

  while (true) {
    if (!this->token.isKeyword(Keyword::Import))
      break;

    SourceLocation begin_loc = this->token.getSourceRange().getBegin();

    if (!this->advanceToken()->is<StringToken>())
      this->error("expected string literal");

    std::string path = std::move(this->token.getStringValue());

    if (!this->advanceToken()->isPunctuator(Punctuator::Semicolon))
      this->error("expected ';'");

    SourceLocation end_loc = this->token.getSourceRange().getEnd();
    SourceRange range(begin_loc, end_loc);

    imports.emplace_back(std::move(path), range);

    this->advanceToken();
  }

  return imports;
}

std::vector<SourceItem> Parser::parseSourceItems() {
  std::vector<SourceItem> items;

  while (true) {
    if (KeywordToken *tok = this->token.get_if<KeywordToken>()) {
      switch (tok->value) {
      case Keyword::Function:
      case Keyword::Extern:
        items.push_back(this->parseFunction());
        continue;
      case Keyword::Constant:
        items.push_back(this->parseVariable());
        continue;
      case Keyword::Struct:
        items.push_back(this->parseStruct());
        continue;
      case Keyword::Variant:
        items.push_back(this->parseVariant());
        continue;
      case Keyword::Enum:
        items.push_back(this->parseEnum());
        continue;
      default:
        break;
      }
    } else if (this->token.is<EOFToken>()) {
      break;
    }

    this->error("expected function, variable, struct, variant, enum or type alias");
  }

  return items;
}

std::unique_ptr<Function> Parser::parseFunction() {
  bool is_extern;

  if (this->token.isKeyword(Keyword::Extern)) {
    is_extern = true;
    this->advanceToken();
  } else {
    assert(this->token.isKeyword(Keyword::Function));
    is_extern = false;
  }

  // SourceLocation begin_loc = this->token.getSourceRange().getBegin();

  this->advanceToken();

  FunctionHeader header = this->parseFunctionHeader();
  std::optional<Qualifier> qualifier = header.qualifier != nullptr ? std::optional<Qualifier>(std::move(header.qualifier)) : std::nullopt;

  std::unique_ptr<Scope> scope = std::make_unique<Scope>(this->scope_stack.back());
  std::unique_ptr<Function> func = std::make_unique<Function>(std::move(qualifier), std::move(header.name), header.name_loc,
                                                              std::move(header.receiver), std::move(scope), is_extern, false);

  if (!this->token.isPunctuator(Punctuator::Lparen))
    this->error("expected '('");

  this->advanceToken();
  this->parseParameterList(&func->params, false);

  if (!this->token.isPunctuator(Punctuator::Rparen))
    this->error("expected ')'");

  if (this->advanceToken()->isKeyword(Keyword::Returns)) {
    SourceLocation returns_loc = this->token.getSourceRange().getBegin();

    if (!this->advanceToken()->isPunctuator(Punctuator::Lparen))
      this->error("expected '('");

    this->advanceToken();
    this->parseParameterList(&func->ret_params, true);

    if (!this->token.isPunctuator(Punctuator::Rparen))
      this->error("expected ')'");

    SourceLocation rparen_loc = this->token.getSourceRange().getEnd();
    SourceRange range(returns_loc, rparen_loc);

    this->advanceToken();

    if (func->ret_params.empty())
      this->diag->error(range, "returns-clause must be omitted for empty return parameter list");
  }

  if (this->token.isPunctuator(Punctuator::Semicolon)) {
    this->advanceToken();
    return func;
  }

  this->scope_stack.push_back(func->scope.get());
  std::unique_ptr<BlockStmt> body = this->parseBlockStmt();
  func->body = std::move(body);
  this->scope_stack.pop_back();

  // SourceRange range(begin_loc, body->range.getEnd());
  return func;
}

// return receiver, qualifier, name
Parser::FunctionHeader Parser::parseFunctionHeader() {
  if (this->token.isPunctuator(Punctuator::Lparen)) {
    this->advanceToken();

    if (!this->token.is<IdToken>()) {
      std::unique_ptr<TypeName> type_name = this->parseTypeName();

      if (!this->token.isPunctuator(Punctuator::Rparen))
        this->error("expected ')'");

      // Saw '(' <type-name> ')'
      // It is either an unnamed receiver, or a qualifier
      // Check next token, if '.', then it is qualifier
      if (this->advanceToken()->isPunctuator(Punctuator::Period)) {
        if (!this->advanceToken()->is<IdToken>())
          this->error("expected identifier");

        std::string name = std::move(this->token.getIdValue());
        SourceRange range = this->token.getSourceRange();
        return FunctionHeader(nullptr, std::move(type_name), std::move(name), range.getBegin());
      }

      std::unique_ptr<Parameter> receiver = std::make_unique<Parameter>(std::nullopt, std::nullopt, std::move(type_name), false);
      return this->parseFunctionHeaderQualifiedName(std::move(receiver));
    }

    std::string name = std::move(this->token.getIdValue());
    SourceRange range = this->token.getSourceRange();

    // Saw 'function' '(' <id>
    // <id> could be a parameter or a type name
    // Check next token, if ')', then it is type name
    if (this->advanceToken()->isPunctuator(Punctuator::Rparen)) {
      // Saw 'function' '(' <id> ')'
      // <id> is a type name
      this->advanceToken();
      std::unique_ptr<TypeName> type_name = std::make_unique<IdentifierTypeName>(range, std::move(name));

      if (this->token.isPunctuator(Punctuator::Period)) {
        // Saw 'function' '(' <id> ')' '.'
        // <type-name> (the <id>) is a qualifier

        if (!this->advanceToken()->is<IdToken>())
          this->error("expected identifier");

        std::string name = std::move(this->token.getIdValue());
        SourceRange range = this->token.getSourceRange();

        this->advanceToken();
        return FunctionHeader(nullptr, std::move(type_name), std::move(name), range.getBegin());
      }

      // <type-name> (the <id>) is the receiver type
      std::unique_ptr<Parameter> receiver = std::make_unique<Parameter>(std::nullopt, std::nullopt, std::move(type_name), false);
      return this->parseFunctionHeaderQualifiedName(std::move(receiver));
    }

    // <id> is the receiver name

    std::unique_ptr<TypeName> type_name = this->parseTypeName();

    if (!this->token.isPunctuator(Punctuator::Rparen))
      this->error("expected ')'");

    this->advanceToken();

    std::unique_ptr<Parameter> receiver = std::make_unique<Parameter>(std::move(name), range.getBegin(), std::move(type_name), false);
    return this->parseFunctionHeaderQualifiedName(std::move(receiver));
  }

  std::unique_ptr<TypeName> type_name;

  if (!this->token.is<IdToken>()) {
    type_name = this->parseTypeName();

    if (!this->token.isPunctuator(Punctuator::Period))
      this->error("expected '.'");

    if (!this->advanceToken()->is<IdToken>())
      this->error("expected identifier");

    std::string name = std::move(this->token.getIdValue());
    SourceRange range = this->token.getSourceRange();

    this->advanceToken();

    return FunctionHeader(nullptr, std::move(type_name), std::move(name), range.getBegin());
  }

  std::string name = std::move(this->token.getIdValue());
  SourceRange range = this->token.getSourceRange();

  if (this->advanceToken()->isPunctuator(Punctuator::Period)) {
    std::unique_ptr<TypeName> type_name = std::make_unique<IdentifierTypeName>(range, std::move(name));

    if (!this->advanceToken()->is<IdToken>())
      this->error("expected identifier");

    std::string name = std::move(this->token.getIdValue());
    SourceRange range = this->token.getSourceRange();

    this->advanceToken();
    return FunctionHeader(nullptr, std::move(type_name), std::move(name), range.getBegin());
  }

  return FunctionHeader(nullptr, nullptr, std::move(name), range.getBegin());
}

Parser::FunctionHeader Parser::parseFunctionHeaderQualifiedName(std::unique_ptr<Parameter> receiver) {
  if (!this->token.is<IdToken>())
    this->error("expected identifier");

  std::string name = std::move(this->token.getIdValue());
  SourceRange range = this->token.getSourceRange();

  if (this->advanceToken()->isPunctuator(Punctuator::Period)) {
    std::unique_ptr<TypeName> type_name = std::make_unique<IdentifierTypeName>(range, std::move(name));

    if (!this->advanceToken()->is<IdToken>())
      this->error("expected identifier");

    std::string name = std::move(this->token.getIdValue());
    SourceRange range = this->token.getSourceRange();

    this->advanceToken();
    return FunctionHeader(std::move(receiver), std::move(type_name), std::move(name), range.getBegin());
  }

  return FunctionHeader(std::move(receiver), nullptr, std::move(name), range.getBegin());
}

void Parser::parseParameterList(std::vector<Parameter> *params, bool is_return) {
  if (this->token.isPunctuator(Punctuator::Rparen))
    return;

  while (true) {
    params->push_back(this->parseParameter(is_return));

    if (!this->token.isPunctuator(Punctuator::Comma))
      break;

    this->advanceToken();
  }
}

Parameter Parser::parseParameter(bool is_return) {
  if (!this->token.is<IdToken>()) {
    std::unique_ptr<TypeName> type_name = this->parseTypeName();
    return Parameter(std::nullopt, std::nullopt, std::move(type_name), is_return);
  }

  std::string name = std::move(this->token.getIdValue());
  SourceRange name_range = this->token.getSourceRange();

  this->advanceToken();

  if (!this->token.isPunctuator(Punctuator::Comma)
      && !this->token.isPunctuator(Punctuator::Rparen)) {
    std::unique_ptr<TypeName> type_name = this->parseTypeName();
    return Parameter(std::move(name), name_range.getBegin(), std::move(type_name), is_return);
  } else {
    std::unique_ptr<TypeName> type_name = std::make_unique<IdentifierTypeName>(name_range, std::move(name));
    return Parameter(std::nullopt, std::nullopt, std::move(type_name), is_return);
  }
}

std::unique_ptr<Variable> Parser::parseVariable() {
  assert(this->token.isKeyword(Keyword::Constant));

  if (!this->advanceToken()->is<IdToken>())
    this->error("expected identifier");

  std::string name = std::move(this->token.getIdValue());
  SourceLocation loc = this->token.getSourceRange().getBegin();

  this->advanceToken();

  std::unique_ptr<TypeName> type_name = this->parseTypeName();

  if (!this->token.isPunctuator(Punctuator::ColonEqual))
    this->error("expected ':='");

  this->advanceToken();

  std::unique_ptr<Expr> init = this->parseExpr();

  if (!this->token.isPunctuator(Punctuator::Semicolon))
    this->error("expected ';'");

  this->advanceToken();

  return std::make_unique<Variable>(std::move(name), loc, std::move(type_name), std::move(init), true);
}

std::unique_ptr<Struct> Parser::parseStruct() {
  assert(this->token.isKeyword(Keyword::Struct));
  // SourceLocation begin_loc = this->token.getSourceRange().getBegin();

  if (!this->advanceToken()->is<IdToken>())
    this->error("expected identifier");

  std::string name = std::move(this->token.getIdValue());
  SourceLocation name_loc = this->token.getSourceRange().getBegin();

  std::unique_ptr<Scope> scope = std::make_unique<Scope>(this->scope_stack.back());
  std::unique_ptr<Struct> struct_ = std::make_unique<Struct>(std::move(name), name_loc, std::move(scope));

  if (!this->advanceToken()->isPunctuator(Punctuator::Lcurly))
    this->error("expected '{{'");

  this->advanceToken();
  this->parseStructFields(&struct_->fields);

  if (!this->token.isPunctuator(Punctuator::Rcurly))
    this->error("expected '}}'");

  if (!this->advanceToken()->isPunctuator(Punctuator::Semicolon))
    this->error("expected ';'");

  // SourceLocation end_loc = this->token.getSourceRange().getEnd();
  // SourceRange range(begin_loc, end_loc);

  this->advanceToken();
  return struct_;
}

void Parser::parseStructFields(std::vector<StructField> *fields) {
  while (true) {
    if (!this->token.is<IdToken>())
      break;

    fields->push_back(this->parseStructField());
  }
}

StructField Parser::parseStructField() {
  assert(this->token.is<IdToken>());

  // SourceLocation begin_loc = this->token.getSourceRange().getBegin();

  std::string name = std::move(this->token.getIdValue());
  SourceLocation loc = this->token.getSourceRange().getBegin();

  this->advanceToken();

  std::unique_ptr<TypeName> type_name = this->parseTypeName();

  if (!this->token.isPunctuator(Punctuator::Semicolon))
    this->error("expected ';'");

  // SourceLocation end_loc = this->token.getSourceRange().getEnd();
  // SourceRange range(begin_loc, end_loc);

  this->advanceToken();
  return StructField(std::move(name), loc, std::move(type_name));
}

std::unique_ptr<Variant> Parser::parseVariant() {
  assert(this->token.isKeyword(Keyword::Variant));
  // SourceLocation begin_loc = this->token.getSourceRange().getBegin();

  if (!this->advanceToken()->is<IdToken>())
    this->error("expected identifier");

  std::string name = std::move(this->token.getIdValue());
  SourceLocation loc = this->token.getSourceRange().getBegin();

  std::unique_ptr<Scope> scope = std::make_unique<Scope>(this->scope_stack.back());
  std::unique_ptr<Variant> variant = std::make_unique<Variant>(std::move(name), loc, std::move(scope));

  if (!this->advanceToken()->isPunctuator(Punctuator::Lcurly))
    this->error("expected '{{'");

  this->advanceToken();

  if (!this->token.isPunctuator(Punctuator::Rcurly)) {
    while (true) {
      std::unique_ptr<TypeName> type_name = this->parseTypeName();
      variant->alternatives.push_back(VariantAlternative(std::move(type_name)));

      if (!this->token.isPunctuator(Punctuator::Comma))
        break;

      this->advanceToken();
    }

    if (!this->token.isPunctuator(Punctuator::Rcurly))
      this->error("expected '}}'");
  }

  if (!this->advanceToken()->isPunctuator(Punctuator::Semicolon))
    this->error("expected ';'");

  // SourceLocation end_loc = this->token.getSourceRange().getEnd();
  // SourceRange range(begin_loc, end_loc);

  this->advanceToken();
  return variant;
}

std::unique_ptr<Enum> Parser::parseEnum() {
  assert(this->token.isKeyword(Keyword::Enum));
  // SourceLocation begin_loc = this->token.getSourceRange().getBegin();

  if (!this->advanceToken()->is<IdToken>())
    this->error("expected identifier");

  std::string name = std::move(this->token.getIdValue());
  SourceLocation loc = this->token.getSourceRange().getBegin();

  std::unique_ptr<Scope> scope = std::make_unique<Scope>(this->scope_stack.back());
  std::unique_ptr<Enum> enum_ = std::make_unique<Enum>(std::move(name), loc, std::move(scope));

  if (!this->advanceToken()->isPunctuator(Punctuator::Lcurly))
    this->error("expected '{{'");

  this->advanceToken();

  while (true) {
    if (!this->token.is<IdToken>())
      this->error("expected identifier");

    std::string value_name = std::move(this->token.getIdValue());
    SourceLocation value_loc = this->token.getSourceRange().getBegin();

    enum_->values.push_back(EnumValue(std::move(value_name), value_loc));

    if (!this->advanceToken()->isPunctuator(Punctuator::Comma))
      break;

    this->advanceToken();
  }

  if (!this->token.isPunctuator(Punctuator::Rcurly))
    this->error("expected '}}'");

  if (!this->advanceToken()->isPunctuator(Punctuator::Semicolon))
    this->error("expected ';'");

  // SourceLocation end_loc = this->token.getSourceRange().getEnd();
  // SourceRange range(begin_loc, end_loc);

  this->advanceToken();
  return enum_;
}

std::unique_ptr<TypeAlias> Parser::parseTypeAlias() {
  assert(this->token.isKeyword(Keyword::Type));

  if (!this->advanceToken()->is<IdToken>())
    this->error("expected identifier");

  std::string name = std::move(this->token.getIdValue());
  SourceLocation loc = this->token.getSourceRange().getBegin();

  if (!this->advanceToken()->isKeyword(Keyword::Is))
    this->error("expected keyword 'is'");

  this->advanceToken();
  std::unique_ptr<TypeName> type_name = this->parseTypeName();

  if (!this->token.isPunctuator(Punctuator::Semicolon))
    this->error("expected ';'");

  this->advanceToken();

  return std::make_unique<TypeAlias>(std::move(name), loc, std::move(type_name));
}

// Types names

std::unique_ptr<TypeName> Parser::parseTypeName() {
  Token *token = &this->token;

  if (token->isElementaryTypeName()) {
    std::unique_ptr<TypeName> type_name = std::make_unique<ElementaryTypeName>(token->getSourceRange(), token->getKeywordValue());
    this->advanceToken();
    return type_name;
  }

  if (token->isPunctuator(Punctuator::Asterisk) || token->isPunctuator(Punctuator::Caret)) {
    SourceLocation begin_loc = token->getSourceRange().getBegin();
    bool is_owning = token->isPunctuator(Punctuator::Caret);
    this->advanceToken();
    std::unique_ptr<TypeName> pointee = this->parseTypeName();
    SourceRange range(begin_loc, pointee->range.getEnd());
    return std::make_unique<PointerTypeName>(range, std::move(pointee), is_owning);
  }

  if (token->isPunctuator(Punctuator::Lsquare)) {
    SourceLocation begin_loc = token->getSourceRange().getBegin();
    this->advanceToken();

    std::unique_ptr<Expr> length = nullptr;
    bool is_slice = false;

    if (token->isPunctuator(Punctuator::Asterisk)) {
      is_slice = true;
      this->advanceToken();
    } else if (!token->isPunctuator(Punctuator::Rsquare)) {
      length = this->parseExpr();
    }

    if (!this->token.isPunctuator(Punctuator::Rsquare))
      this->error("expected ']'");

    this->advanceToken();
    std::unique_ptr<TypeName> element = this->parseTypeName();

    SourceRange range(begin_loc, element->range.getEnd());

    if (is_slice)
      return std::make_unique<SliceTypeName>(range, std::move(element));
    return std::make_unique<ArrayTypeName>(range, std::move(element), std::move(length));
  }

  if (token->is<IdToken>()) {
    std::unique_ptr<TypeName> type_name = std::make_unique<IdentifierTypeName>(token->getSourceRange(), std::move(token->getIdValue()));
    this->advanceToken();
    return type_name;
  }

  if (token->isPunctuator(Punctuator::Lparen)) {
    SourceLocation begin_loc = token->getSourceRange().getBegin();
    this->advanceToken();

    std::unique_ptr<TypeName> type_name = this->parseTypeName();

    if (!this->token.isPunctuator(Punctuator::Rparen))
      this->error("expected ')'");

    SourceLocation end_loc = token->getSourceRange().getEnd();
    this->advanceToken();

    SourceRange range(begin_loc, end_loc);
    return std::make_unique<ParenthesizedTypeName>(range, std::move(type_name));
  }

  this->error("expected type name");
}

// Statements.

void Parser::checkVariableDecl(const Stmt *stmt) {
  if (stmt->isVarDeclStmt())
    this->diag->error(stmt->range, "variable declaration is only permitted inside a block");
}

std::unique_ptr<Stmt> Parser::parseStmt() {
  Token *token = &this->token;

  if (token->isPunctuator(Punctuator::Lcurly))
    return this->parseBlockStmt();

  if (token->isKeyword(Keyword::If))
    return this->parseIfStmt();

  if (token->isKeyword(Keyword::For))
    return this->parseForStmt();

  if (token->isKeyword(Keyword::While))
    return this->parseWhileStmt();

  if (token->is<IdToken>() && this->peekToken()->isPunctuator(Punctuator::Colon)) {
    std::string label = std::move(token->getIdValue());
    SourceLocation begin_loc = token->getSourceRange().getBegin();

    this->advanceToken();
    this->advanceToken();

    std::unique_ptr<Stmt> stmt = this->parseStmt();

    if (!stmt->isWhileStmt() && !stmt->isForStmt())
      this->diag->error(begin_loc, "only 'for' and 'while' can be labeled");

    SourceLocation end_loc = stmt->range.getEnd();
    SourceRange range(begin_loc, end_loc);

    return std::make_unique<LabeledStmt>(range, std::move(label), std::move(stmt));
  }

  if (token->isKeyword(Keyword::Switch))
    return this->parseSwitchStmt();

  if (token->isKeyword(Keyword::Fallthrough) || token->isKeyword(Keyword::Skip)) {
    bool is_fallthrough = token->isKeyword(Keyword::Fallthrough);
    SourceLocation begin_loc = token->getSourceRange().getBegin();

    if (!this->advanceToken()->isPunctuator(Punctuator::Semicolon))
      this->error("expected ';'");

    SourceLocation end_loc = this->token.getSourceRange().getEnd();
    this->advanceToken();

    SourceRange range(begin_loc, end_loc);
    if (is_fallthrough)
      return std::make_unique<FallthroughStmt>(range);
    else
      return std::make_unique<SkipStmt>(range);
  }

  if (token->isKeyword(Keyword::Break) || token->isKeyword(Keyword::Continue)) {
    bool is_break = token->isKeyword(Keyword::Break);

    SourceLocation begin_loc = token->getSourceRange().getBegin();

    std::optional<std::string> label;
    if (this->advanceToken()->is<IdToken>()) {
      label = std::move(this->token.getIdValue());
      this->advanceToken();
    }

    if (!this->token.isPunctuator(Punctuator::Semicolon))
      this->error("expected ';'");

    SourceLocation end_loc = this->token.getSourceRange().getEnd();
    this->advanceToken();

    SourceRange range(begin_loc, end_loc);

    if (is_break)
      return std::make_unique<BreakStmt>(range, label);
    return std::make_unique<ContinueStmt>(range, label);
  }

  if (this->expressionStartsHere())
    return this->parseExprStmt();

  if (token->isKeyword(Keyword::Var))
    return this->parseVarDeclStmt();

  if (token->isKeyword(Keyword::Return)) {
    SourceLocation begin_loc = token->getSourceRange().getBegin();

    this->advanceToken();

    std::unique_ptr<Expr> expr = nullptr;
    if (!this->token.isPunctuator(Punctuator::Semicolon)) {
      expr = this->parseExpr();

      if (!this->token.isPunctuator(Punctuator::Semicolon))
        this->error("expected ';'");
    };

    SourceLocation end_loc = this->token.getSourceRange().getEnd();
    SourceRange range(begin_loc, end_loc);

    this->advanceToken();

    return std::make_unique<ReturnStmt>(range, std::move(expr));
  }

  this->error("expected statement");
}

std::unique_ptr<BlockStmt> Parser::parseBlockStmt() {
  if (!this->token.isPunctuator(Punctuator::Lcurly))
    this->error("expected '{{'");

  SourceLocation begin_loc = this->token.getSourceRange().getBegin();
  this->advanceToken();

  std::vector<std::unique_ptr<Stmt>> stmts;

  std::unique_ptr<Scope> scope = std::make_unique<Scope>(this->scope_stack.back());
  this->scope_stack.push_back(scope.get());

  while (true) {
    if (this->token.isPunctuator(Punctuator::Rcurly))
      break;
    stmts.push_back(this->parseStmt());
  }

  this->scope_stack.pop_back();

  SourceLocation end_loc = this->token.getSourceRange().getEnd();
  SourceRange range(begin_loc, end_loc);

  this->advanceToken();
  return std::make_unique<BlockStmt>(range, std::move(stmts), std::move(scope));
}

std::unique_ptr<IfStmt> Parser::parseIfStmt() {
  assert(this->token.isKeyword(Keyword::If));
  SourceLocation begin_loc = this->token.getSourceRange().getBegin();

  if (!this->advanceToken()->isPunctuator(Punctuator::Lparen))
    this->error("expected '('");

  this->advanceToken();
  std::unique_ptr<Expr> cond = this->parseExpr();

  if (!this->token.isPunctuator(Punctuator::Rparen))
    this->error("expected ')'");

  this->advanceToken();
  std::unique_ptr<Stmt> then = this->parseStmt();
  this->checkVariableDecl(then.get());

  std::unique_ptr<Stmt> else_ = nullptr;
  if (this->token.isKeyword(Keyword::Else)) {
    this->advanceToken();
    else_ = this->parseStmt();
    this->checkVariableDecl(else_.get());
  }

  SourceLocation end_loc = (else_ != nullptr
                            ? else_->range.getEnd()
                            : then->range.getEnd());
  SourceRange range(begin_loc, end_loc);

  return std::make_unique<IfStmt>(range, std::move(cond), std::move(then), std::move(else_));
}

std::unique_ptr<ForStmt> Parser::parseForStmt() {
  assert(this->token.isKeyword(Keyword::For));
  SourceLocation begin_loc = this->token.getSourceRange().getBegin();

  if (!this->advanceToken()->isPunctuator(Punctuator::Lparen))
    this->error("expected '('");

  this->advanceToken();

  std::unique_ptr<Scope> scope = std::make_unique<Scope>(this->scope_stack.back());
  this->scope_stack.push_back(scope.get());

  std::unique_ptr<Stmt> init = nullptr;
  if (this->token.isPunctuator(Punctuator::Semicolon)) {
    this->advanceToken();
  } else if (this->token.isKeyword(Keyword::Var)) {
    init = this->parseVarDeclStmt();
  } else {
    init = this->parseExprStmt();
  }

  std::unique_ptr<Expr> cond = nullptr;
  if (!this->token.isPunctuator(Punctuator::Semicolon)) {
    cond = this->parseExpr();
    if (!this->token.isPunctuator(Punctuator::Semicolon))
      this->error("expected ';'");
  }

  this->advanceToken();

  std::unique_ptr<Expr> step = nullptr;
  if (!this->token.isPunctuator(Punctuator::Rparen)) {
    step = this->parseExpr();
    if (!this->token.isPunctuator(Punctuator::Rparen))
      this->error("expected ')'");
  }

  this->advanceToken();

  std::unique_ptr<Stmt> body = this->parseStmt();
  this->scope_stack.pop_back();
  this->checkVariableDecl(body.get());

  SourceLocation end_loc = body->range.getEnd();
  SourceRange range(begin_loc, end_loc);

  return std::make_unique<ForStmt>(range, std::move(init), std::move(cond), std::move(step), std::move(body), std::move(scope));
}

std::unique_ptr<WhileStmt> Parser::parseWhileStmt() {
  assert(this->token.isKeyword(Keyword::While));
  SourceLocation begin_loc = this->token.getSourceRange().getBegin();

  if (!this->advanceToken()->isPunctuator(Punctuator::Lparen))
    this->error("expected '('");

  this->advanceToken();
  std::unique_ptr<Expr> cond = this->parseExpr();

  if (!this->token.isPunctuator(Punctuator::Rparen))
    this->error("expected ')'");

  this->advanceToken();
  std::unique_ptr<Stmt> body = this->parseStmt();
  this->checkVariableDecl(body.get());

  SourceLocation end_loc = body->range.getEnd();
  SourceRange range(begin_loc, end_loc);

  return std::make_unique<WhileStmt>(range, std::move(cond), std::move(body));
}

std::unique_ptr<SwitchStmt> Parser::parseSwitchStmt() {
  assert(this->token.isKeyword(Keyword::Switch));
  SourceLocation begin_loc = this->token.getSourceRange().getBegin();

  this->advanceToken();

  std::unique_ptr<Expr> cond = nullptr;

  if (this->token.isPunctuator(Punctuator::Lparen)) {
    this->advanceToken();
    cond = this->parseExpr();

    if (!this->token.isPunctuator(Punctuator::Rparen))
      this->error("expected ')'");

    this->advanceToken();
  }

  if (!this->token.isPunctuator(Punctuator::Lcurly))
    this->error("expected '{{'");

  this->advanceToken();

  std::vector<SwitchClause> clauses  = this->parseSwitchClauses();

  if (!this->token.isPunctuator(Punctuator::Rcurly))
    this->error("expected '}}'");

  SourceLocation end_loc = this->token.getSourceRange().getEnd();
  SourceRange range(begin_loc, end_loc);

  this->advanceToken();
  return std::make_unique<SwitchStmt>(range, std::move(cond), std::move(clauses));
}

std::vector<SwitchClause> Parser::parseSwitchClauses() {
  std::vector<SwitchClause> clauses;

  while (true) {
    if (!this->token.isKeyword(Keyword::Case) && !this->token.isKeyword(Keyword::Default))
      break;

    SourceLocation label_loc = this->token.getSourceRange().getBegin();

    std::unique_ptr<Expr> label = nullptr;
    if (this->token.isKeyword(Keyword::Case)) {
      this->advanceToken();
      label = this->parseExpr();
    } else {
      this->advanceToken();
    }

    if (!this->token.isPunctuator(Punctuator::Colon))
      this->error("expected ':'");

    this->advanceToken();

    std::vector<std::unique_ptr<Stmt>> body;
    while (true) {
      if (this->token.isPunctuator(Punctuator::Rcurly)
          || this->token.isKeyword(Keyword::Case)
          || this->token.isKeyword(Keyword::Default))
        break;
      std::unique_ptr<Stmt> stmt = this->parseStmt();
      this->checkVariableDecl(stmt.get());
      body.push_back(std::move(stmt));
    }

    clauses.push_back(SwitchClause(std::move(label), std::move(body), label_loc));
  }

  return clauses;
}

std::unique_ptr<ExprStmt> Parser::parseExprStmt() {
  std::unique_ptr<Expr> expr = this->parseExpr();

  if (!this->token.isPunctuator(Punctuator::Semicolon))
    this->error("expected ';'");

  SourceRange range(expr->range.getBegin(), this->token.getSourceRange().getEnd());
  this->advanceToken();

  return std::make_unique<ExprStmt>(range, std::move(expr));
}

std::unique_ptr<VarDeclStmt> Parser::parseVarDeclStmt() {
  assert(this->token.isKeyword(Keyword::Var));

  SourceLocation begin_loc = this->token.getSourceRange().getBegin();

  this->advanceToken();

  std::vector<std::unique_ptr<Variable>> vars;

  while (true) {
    if (!this->token.is<IdToken>())
      this->error("expected identifier");

    std::string name = std::move(this->token.getIdValue());
    SourceLocation loc = this->token.getSourceRange().getBegin();

    this->advanceToken();

    std::unique_ptr<TypeName> type_name = nullptr;

    if (!this->token.isPunctuator(Punctuator::Comma)
        && !this->token.isPunctuator(Punctuator::Semicolon)
        && !this->token.isPunctuator(Punctuator::ColonEqual))
      type_name = this->parseTypeName();

    vars.push_back(std::make_unique<Variable>(std::move(name), loc, std::move(type_name), nullptr, false));

    if (!this->token.isPunctuator(Punctuator::Comma))
      break;
    this->advanceToken();
  }

  std::unique_ptr<Expr> init = nullptr;
  if (this->token.isPunctuator(Punctuator::ColonEqual)) {
    this->advanceToken();
    init = this->parseExpr();
  }

  if (!this->token.isPunctuator(Punctuator::Semicolon))
    this->error("expected ';'");

  SourceLocation end_loc = this->token.getSourceRange().getEnd();
  this->advanceToken();

  SourceRange range(begin_loc, end_loc);

  return std::make_unique<VarDeclStmt>(range, std::move(vars), std::move(init));
}

// Expressions.

bool Parser::expressionStartsHere() const {
  const Token *token = &this->token;

  if (token->is<IdToken>())
    return true;

  if (token->is<IntToken>())
    return true;

  if (token->is<StringToken>())
    return true;

  if (token->is<CharToken>())
    return true;

  if (token->isKeyword(Keyword::True)
      || token->isKeyword(Keyword::False)
      || token->isKeyword(Keyword::Null)
      || token->isKeyword(Keyword::New))
    return true;

  if (token->isPunctuator(Punctuator::Lparen))
    return true;

  if (token->isElementaryTypeName()
      || token->isPunctuator(Punctuator::Lsquare)
      || token->isPunctuator(Punctuator::Caret))
    return true;

  return (token->isPunctuator(Punctuator::Plus)
          || token->isPunctuator(Punctuator::Minus)
          || token->isPunctuator(Punctuator::Asterisk)
          || token->isPunctuator(Punctuator::Amp)
          || token->isPunctuator(Punctuator::Exclaim)
          || token->isPunctuator(Punctuator::Tilde));
}

std::unique_ptr<Expr> Parser::parseExpr() {
  return this->parseExpr({});
}

std::unique_ptr<Expr> Parser::parseExpr(std::optional<InfixOp> left_op) {
  std::unique_ptr<Expr> left = this->parseTypeTestExpr();

  while (true) {
    std::optional<InfixOp> op_opt = this->token.toInfixOp();

    // Token is not an infix operator.
    if (!op_opt.has_value())
      return left;

    InfixOp op = op_opt.value();
    SourceRange op_range = this->token.getSourceRange();

    Precedence prec = getPrecedence(op);

    if (left_op.has_value()
        && ((getPrecedence(left_op.value()) > prec)
            || (getPrecedence(left_op.value()) == prec && isLeftAssociative(prec))))
      return left;

    this->advanceToken();

    std::unique_ptr<Expr> right = this->parseExpr(op);

    SourceRange range(left->range.getBegin(), right->range.getEnd());

    if (std::holds_alternative<BinaryOp>(op))
      left = this->checkBinaryExprPrecedence(range, std::get<BinaryOp>(op), std::move(left), std::move(right), op_range);
    else
      left = std::make_unique<AssignmentExpr>(range, std::get<AssignmentOp>(op), std::move(left), std::move(right), op_range);
  }
}

void Parser::precedenceError(BinaryOp op, SourceRange op_range, const BinaryExpr *operand) {
  DiagSourceInfo src_info(operand->op_range.getBegin(), {op_range, operand->range});

  this->diag->error(src_info, "'{}' within '{}' must be parenthesized",
                    binaryOpToString(operand->op), binaryOpToString(op));
}

std::unique_ptr<Expr> Parser::checkBinaryExprPrecedence(SourceRange range,
                                                        BinaryOp op,
                                                        std::unique_ptr<Expr> left,
                                                        std::unique_ptr<Expr> right,
                                                        SourceRange op_range) {
  switch (getPrecedence(op)) {
  case Precedence::Or:
  case Precedence::And: {
    // Allow operands of the same operator or a relational operator.
    if (left->isBinaryExpr() && !(left->toBinaryExpr()->op == op || getPrecedence(left->toBinaryExpr()->op) == Precedence::Relational))
      this->precedenceError(op, op_range, left->toBinaryExpr());
    if (right->isBinaryExpr() && getPrecedence(right->toBinaryExpr()->op) != Precedence::Relational)
      this->precedenceError(op, op_range, right->toBinaryExpr());
    break;
  }
  case Precedence::Relational: {
    // Allow operands of additive or multiplicative operator.
    if (left->isBinaryExpr()) {
      Precedence prec = getPrecedence(left->toBinaryExpr()->op);
      if (prec != Precedence::Additive && prec != Precedence::Multiplicative)
        this->precedenceError(op, op_range, left->toBinaryExpr());
    }
    if (right->isBinaryExpr()) {
      Precedence prec = getPrecedence(right->toBinaryExpr()->op);
      if (prec != Precedence::Additive && prec != Precedence::Multiplicative)
        this->precedenceError(op, op_range, right->toBinaryExpr());
    }
    break;
  }
  case Precedence::BitOr:
  case Precedence::BitXor:
  case Precedence::BitAnd:
  case Precedence::Shift: {
    // Allow nothing.
    if (left->isBinaryExpr())
      this->precedenceError(op, op_range, left->toBinaryExpr());
    if (right->isBinaryExpr())
      this->precedenceError(op, op_range, right->toBinaryExpr());
    break;
  }
  case Precedence::Additive: {
    // Allow operands of additive or multiplicative operator (always true).
    break;
  }
  case Precedence::Multiplicative: {
    // Allow operands of multiplicative operator (always true).
    break;
  }
  default:
    assert(false && "unknown precedence");
  }

  return std::make_unique<BinaryExpr>(range, op, std::move(left), std::move(right), op_range);
}

std::unique_ptr<Expr> Parser::parseTypeTestExpr() {
  std::unique_ptr<Expr> expr = this->parseCastExpr();

  while (this->token.isKeyword(Keyword::Is)) {
    bool is_negated;

    if (this->advanceToken()->isKeyword(Keyword::Not)) {
      is_negated = true;
      this->advanceToken();
    } else {
      is_negated = false;
    }

    std::unique_ptr<TypeName> type_name = std::unique_ptr<TypeName>(this->parseTypeName());
    SourceRange range(expr->range.getBegin(), type_name->range.getEnd());

    expr = std::make_unique<TypeTestExpr>(range, std::move(expr), std::move(type_name), is_negated);
  }

  return expr;
}

std::unique_ptr<Expr> Parser::parseCastExpr() {
  std::unique_ptr<Expr> expr = this->parseUnaryExpr();

  while (this->token.isKeyword(Keyword::As)
         || this->token.isKeyword(Keyword::AsExclaim)) {
    bool is_exclaimed = this->token.isKeyword(Keyword::AsExclaim);
    this->advanceToken();

    std::unique_ptr<TypeName> type_name = std::unique_ptr<TypeName>(this->parseTypeName());
    SourceRange range(expr->range.getBegin(), type_name->range.getEnd());

    expr = std::make_unique<TypeCastExpr>(range, std::move(expr), std::move(type_name), is_exclaimed);
  }

  return expr;
}

std::unique_ptr<Expr> Parser::parseUnaryExpr() {
  const Token *token = &this->token;

  std::optional<UnaryOp> op = token->toUnaryOp();

  if (op.has_value()) {
    SourceRange op_range = token->getSourceRange();
    this->advanceToken();
    std::unique_ptr<Expr> expr = this->parseUnaryExpr();
    SourceRange range(op_range.getBegin(), expr->range.getEnd());
    return std::make_unique<UnaryExpr>(range, op.value(), std::move(expr), op_range);
  }

  return this->parsePostfixExpr();
}

std::vector<CallArg> Parser::parseCallArgs() {
  std::vector<CallArg> args;

  if (this->token.isPunctuator(Punctuator::Rparen))
    return args;

  while (true) {
    if (this->token.is<IdToken>() && this->peekToken()->isPunctuator(Punctuator::Colon)) {
      CallArg::Name name(std::move(this->token.getIdValue()), this->token.getSourceRange());

      // Consume both the identifier and the ':'
      this->advanceToken();
      this->advanceToken();

      args.emplace_back(this->parseExpr(), std::move(name));
    } else {
      args.emplace_back(this->parseExpr(), std::nullopt);
    }

    if (!this->token.isPunctuator(Punctuator::Comma))
      break;
    this->advanceToken();
  }

  return args;
}

std::unique_ptr<Expr> Parser::parsePostfixExpr() {
  std::unique_ptr<Expr> expr = this->parsePrimaryExpr();

  while (true) {
    const Token *token = &this->token;

    if (token->isPunctuator(Punctuator::Lparen)) {
      SourceLocation lparen_loc = token->getSourceRange().getBegin();
      this->advanceToken();

      std::vector<CallArg> arguments = this->parseCallArgs();

      if (!this->token.isPunctuator(Punctuator::Rparen))
        this->error("expected ')'");

      SourceLocation end_loc = this->token.getSourceRange().getEnd();
      SourceRange range(expr->range.getBegin(), end_loc);

      this->advanceToken();

      expr = std::make_unique<CallExpr>(range, std::move(expr), std::move(arguments), lparen_loc);
      continue;
    }

    if (token->isPunctuator(Punctuator::Lsquare)) {
      SourceLocation lsquare_loc = token->getSourceRange().getBegin();
      this->advanceToken();
      std::unique_ptr<Expr> index = this->parseExpr();

      if (!this->token.isPunctuator(Punctuator::Rsquare))
        this->error("expected ']'");

      SourceLocation end_loc = this->token.getSourceRange().getEnd();
      SourceRange range(expr->range.getBegin(), end_loc);

      this->advanceToken();

      expr = std::make_unique<IndexAccessExpr>(range, std::move(expr), std::move(index), lsquare_loc);
      continue;
    }

    if (token->isPunctuator(Punctuator::Period) || token->isPunctuator(Punctuator::Arrow)) {
      SourceLocation dot_loc = token->getSourceRange().getBegin();
      bool is_arrow = token->isPunctuator(Punctuator::Arrow);

      if (this->advanceToken()->is<IdToken>()) {
        std::string member_name = std::move(this->token.getIdValue());
        SourceRange member_range = this->token.getSourceRange();

        SourceLocation end_loc = member_range.getEnd();
        SourceRange range(expr->range.getBegin(), end_loc);

        this->advanceToken();

        expr = std::make_unique<MemberAccessExpr>(range, std::move(expr), std::move(member_name), is_arrow, dot_loc, member_range);
        continue;
      } else if (this->token.isPunctuator(Punctuator::Lparen)) {
        this->advanceToken();

        std::unique_ptr<TypeName> alternative = this->parseTypeName();

        if (!this->token.isPunctuator(Punctuator::Rparen))
          this->error("expected ')'");

        SourceLocation end_loc = this->token.getSourceRange().getEnd();
        SourceRange range(expr->range.getBegin(), end_loc);

        this->advanceToken();

        expr = std::make_unique<VariantAccessExpr>(range, std::move(expr), std::move(alternative), is_arrow, dot_loc);
        continue;
      } else {
        this->error("expected identifier or '('");
      }
    }

    return expr;
  }
}

std::unique_ptr<Expr> Parser::parsePrimaryExpr() {
  Token *token = &this->token;

  if (token->is<IntToken>()) {
    std::unique_ptr<Expr> expr = std::make_unique<IntLiteralExpr>(token->getSourceRange(), std::move(token->getIntValue()));
    this->advanceToken();
    return expr;
  }

  if (token->is<CharToken>()) {
    std::unique_ptr<Expr> expr = std::make_unique<CharLiteralExpr>(token->getSourceRange(), token->getCharValue());
    this->advanceToken();
    return expr;
  }

  if (token->is<StringToken>()) {
    std::unique_ptr<Expr> expr = std::make_unique<StringLiteralExpr>(token->getSourceRange(), std::move(token->getStringValue()));
    this->advanceToken();
    return expr;
  }

  if (token->is<IdToken>()) {
    std::unique_ptr<Expr> expr = std::make_unique<IdentifierExpr>(token->getSourceRange(), std::move(token->getIdValue()));
    this->advanceToken();
    return expr;
  }

  if (token->isPunctuator(Punctuator::Lparen) || token->isPunctuator(Punctuator::Lcurly)) {
    bool is_composite_literal = token->isPunctuator(Punctuator::Lcurly);

    Punctuator closing = is_composite_literal ? Punctuator::Rcurly : Punctuator::Rparen;

    SourceLocation begin_loc = token->getSourceRange().getBegin();
    this->advanceToken();

    std::vector<std::unique_ptr<Expr>> exprs;

    if (!this->token.isPunctuator(closing)) {
      while (true) {
        exprs.push_back(std::unique_ptr<Expr>(this->parseExpr()));

        if (!this->token.isPunctuator(Punctuator::Comma))
          break;
        this->advanceToken();
      }

      if (!this->token.isPunctuator(closing))
        this->error(fmt::format("expected '{}'", is_composite_literal ? '}' : ')'));
    }

    SourceLocation end_loc = this->token.getSourceRange().getEnd();
    SourceRange range(begin_loc, end_loc);

    this->advanceToken();

    if (is_composite_literal)
      return std::make_unique<CompositeLiteralExpr>(range, std::move(exprs));
    else if (exprs.size() == 1)
      return std::make_unique<ParenExpr>(range, std::move(exprs.front()));
    else
      return std::make_unique<ListExpr>(range, std::move(exprs));
  }

  if (token->isKeyword(Keyword::True) || token->isKeyword(Keyword::False)) {
    std::unique_ptr<Expr> expr = std::make_unique<BoolLiteralExpr>(token->getSourceRange(), token->isKeyword(Keyword::True));
    this->advanceToken();
    return expr;
  }

  if (token->isKeyword(Keyword::New)) {
    SourceLocation begin_loc = token->getSourceRange().getBegin();
    this->advanceToken();
    std::unique_ptr<TypeName> type_name = this->parseTypeName();
    SourceRange range(begin_loc, type_name->range.getEnd());
    return std::make_unique<NewExpr>(range, std::move(type_name));
  }

  if (token->isElementaryTypeName()
      || token->isPunctuator(Punctuator::Lsquare)
      || token->isPunctuator(Punctuator::Caret)) {
    std::unique_ptr<TypeName> type_name = this->parseTypeName();
    return std::make_unique<TypeNameExpr>(type_name->range, std::move(type_name));
  }

  if (token->isKeyword(Keyword::Null)) {
    std::unique_ptr<Expr> expr = std::make_unique<NullExpr>(token->getSourceRange());
    this->advanceToken();
    return expr;
  }

  this->error("expected expression");
}
