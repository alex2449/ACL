#pragma once

#include <optional>
#include <string>
#include <vector>

#include "acl.hpp"
#include "lexer.hpp"

class Diagnostics;
struct Token;

class Parser {
 public:
  Parser(Lexer *lexer, Diagnostics *diag)
      : lexer(lexer), diag(diag), token(lexer->lexNextToken()) { }

  std::unique_ptr<SourceUnit> parse(std::string path);

 private:
  Token *advanceToken();
  Token *peekToken();
  [[ noreturn ]] void error(const std::string &message) const;

  struct FunctionHeader {
    FunctionHeader(std::unique_ptr<Parameter> receiver, std::unique_ptr<TypeName> qualifier, std::string name, SourceLocation name_loc)
        : receiver(std::move(receiver)), qualifier(std::move(qualifier)), name(std::move(name)), name_loc(name_loc) { }
    std::unique_ptr<Parameter> receiver;
    std::unique_ptr<TypeName> qualifier;
    std::string name;
    SourceLocation name_loc;
  };

  // Source items
  std::vector<Import> parseImports();
  std::vector<SourceItem> parseSourceItems();
  std::unique_ptr<Function> parseFunction();
  FunctionHeader parseFunctionHeader();
  FunctionHeader parseFunctionHeaderQualifiedName(std::unique_ptr<Parameter> receiver);
  void parseParameterList(std::vector<Parameter> *params, bool is_return);
  Parameter parseParameter(bool is_return);
  std::unique_ptr<Variable> parseVariable();
  std::unique_ptr<Struct> parseStruct();
  void parseStructFields(std::vector<StructField> *fields);
  StructField parseStructField();
  std::unique_ptr<Variant> parseVariant();
  std::unique_ptr<Enum> parseEnum();
  std::unique_ptr<TypeAlias> parseTypeAlias();

  // Type names
  std::unique_ptr<TypeName> parseTypeName();

  // Statements
  void checkVariableDecl(const Stmt *stmt);
  std::unique_ptr<Stmt> parseStmt();
  std::unique_ptr<VarDeclStmt> parseVarDeclStmt();
  std::unique_ptr<BlockStmt> parseBlockStmt();
  std::unique_ptr<IfStmt> parseIfStmt();
  std::unique_ptr<ForStmt> parseForStmt();
  std::unique_ptr<WhileStmt> parseWhileStmt();
  std::unique_ptr<SwitchStmt> parseSwitchStmt();
  std::vector<SwitchClause> parseSwitchClauses();
  std::unique_ptr<ExprStmt> parseExprStmt();

  // Expressions
  bool expressionStartsHere() const;
  std::unique_ptr<Expr> parseExpr();
  std::unique_ptr<Expr> parseExpr(std::optional<InfixOp> left_op);
  void precedenceError(BinaryOp op, SourceRange op_range, const BinaryExpr *operand);
  std::unique_ptr<Expr> checkBinaryExprPrecedence(SourceRange range,
                                                  BinaryOp op,
                                                  std::unique_ptr<Expr> left_,
                                                  std::unique_ptr<Expr> right_,
                                                  SourceRange op_range);
  std::unique_ptr<Expr> parseTypeTestExpr();
  std::unique_ptr<Expr> parseCastExpr();
  std::unique_ptr<Expr> parseUnaryExpr();
  std::vector<CallArg> parseCallArgs();
  std::unique_ptr<Expr> parsePostfixExpr();
  std::unique_ptr<Expr> parsePrimaryExpr();

  Lexer *lexer;
  Diagnostics *diag;
  // The current token
  Token token;
  // A token used as a lookahead
  std::optional<Token> next;

  std::vector<Scope *> scope_stack;
};
