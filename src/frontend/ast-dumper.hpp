#pragma once

#include <ostream>
#include <string_view>
#include <vector>

#include "acl-forward.hpp"
#include "../util/color.hpp"

class ASTDumper {
 public:
  ASTDumper(std::ostream &os, bool with_color) : os(os), with_color(with_color) { }

  void visit(const SourceUnit *su);

 private:
  void indent(unsigned number_of_children);
  void unindent();
  void printIndentation();

  ColorScope withColor(const char *color) const {
    return ColorScope(std::cout, this->with_color, color);
  }

  void dumpNode(std::string_view name, const char *color);

  template <typename T>
  void dumpNullable(const T *node) {
    if (node == nullptr)
      dumpNode("NULL", BOLD_BLACK);
    else
      this->visit(node);
  }

  template <typename T>
  void dumpList(std::string_view name, const char *color, const std::vector<T *> *components) {
    this->dumpNode(name, color);

    if (components->empty())
      return;

    this->indent(components->size());

    for (const T *component : *components)
      this->dumpNullable(component);

    this->unindent();
  }

  template <typename T>
  void dumpList(std::string_view name, const char *color, const std::vector<T> *components) {
    this->dumpNode(name, color);

    if (components->empty())
      return;

    this->indent(components->size());

    for (const T &component : *components)
      this->visit(&component);

    this->unindent();
  }

  void visit(const SourceItem *item);
  void visit(const Function *function);
  void visit(const Parameter *param);
  void visit(const Variable *var);
  void visit(const Struct *struct_);
  void visit(const StructField *field);
  void visit(const Variant *variant);
  void visit(const VariantAlternative *alternative);
  void visit(const Enum *enum_);
  void visit(const EnumValue *value);
  void visit(const TypeAlias *type_alias);

  void beginTypeName(const TypeName *type_name, std::string_view node_kind);
  void visit(const TypeName *type_name);
  void visit(const ElementaryTypeName *type_name);
  void visit(const PointerTypeName *type_name);
  void visit(const ArrayTypeName *type_name);
  void visit(const SliceTypeName *type_name);
  void visit(const IdentifierTypeName *type_name);
  void visit(const ParenthesizedTypeName *type_name);

  void beginStmt(const Stmt *stmt, std::string_view node_kind);
  void visit(const Stmt *stmt);
  void visit(const BlockStmt *stmt);
  void visit(const IfStmt *stmt);
  void visit(const ForStmt *stmt);
  void visit(const WhileStmt *stmt);
  void visit(const LabeledStmt *stmt);
  void visit(const SwitchStmt *stmt);
  void visit(const SwitchClause *stmt);
  void visit(const FallthroughStmt *stmt);
  void visit(const ExprStmt *stmt);
  void visit(const SkipStmt *stmt);
  void visit(const BreakStmt *stmt);
  void visit(const ContinueStmt *stmt);
  void visit(const VarDeclStmt *stmt);
  void visit(const ReturnStmt *stmt);

  void beginExpr(const Expr *expr, std::string_view node_kind);
  void visit(const Expr *expr);
  void visit(const AssignmentExpr *expr);
  void visit(const UnaryExpr *expr);
  void visit(const BinaryExpr *expr);
  void visit(const CallExpr *expr);
  void visit(const IndexAccessExpr *expr);
  void visit(const MemberAccessExpr *expr);
  void visit(const VariantAccessExpr *expr);
  void visit(const TypeTestExpr *expr);
  void visit(const TypeCastExpr *expr);
  void visit(const IdentifierExpr *expr);
  void visit(const ParenExpr *expr);
  void visit(const ListExpr *expr);
  void visit(const CompositeLiteralExpr *expr);
  void visit(const NewExpr *expr);
  void visit(const TypeNameExpr *expr);
  void visit(const IntLiteralExpr *expr);
  void visit(const BoolLiteralExpr *expr);
  void visit(const NullExpr *expr);
  void visit(const StringLiteralExpr *expr);
  void visit(const CharLiteralExpr *expr);

  std::ostream &os;
  // We use this vector to print the tree structure
  std::vector<unsigned> pending_children;
  bool with_color;
};
