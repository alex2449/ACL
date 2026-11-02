#include "acl.hpp"
#include "ast-dumper.hpp"
#include "type.hpp"
#include "../util/util.hpp"
#include "../util/color.hpp"

#include <cassert>

void ASTDumper::indent(unsigned number_of_children) {
  assert(number_of_children > 0);
  this->pending_children.push_back(number_of_children);
}

void ASTDumper::unindent() {
  assert(!this->pending_children.empty());
  this->pending_children.pop_back();
}

void ASTDumper::printIndentation() {
  // Unicode characters taken from https://en.wikipedia.org/wiki/Box-drawing_characters
  for (size_t i = 0, n = this->pending_children.size(); i < n; ++i) {
    unsigned *children = &this->pending_children[i];

    if (*children == 0) {
      this->os << "  ";
    } else if (i < n - 1) {
      this->os << "│ ";
    } else {
      this->os << (*children > 1 ? "├─" : "└─");
      --*children;
    }
  }
}

void ASTDumper::dumpNode(std::string_view name, const char *color) {
  this->printIndentation();
  this->withColor(color) << name << "\n";
}

void ASTDumper::visit(const SourceUnit *su) {
  this->dumpList("SourceItemList", BOLD_HIGH_INTENSITY_YELLOW, &su->items);
}

void ASTDumper::visit(const SourceItem *item) {
  switch (item->getTag()) {
  case SourceItem::Tag::Function:  return this->visit(item->get<std::unique_ptr<Function>>().get());
  case SourceItem::Tag::Variable:  return this->visit(item->get<std::unique_ptr<Variable>>().get());
  case SourceItem::Tag::Struct:    return this->visit(item->get<std::unique_ptr<Struct>>().get());
  case SourceItem::Tag::Variant:   return this->visit(item->get<std::unique_ptr<Variant>>().get());
  case SourceItem::Tag::Enum:      return this->visit(item->get<std::unique_ptr<Enum>>().get());
  case SourceItem::Tag::TypeAlias: return this->visit(item->get<std::unique_ptr<TypeAlias>>().get());
  }

  assert(false);
}

void ASTDumper::visit(const Function *func) {
  this->dumpNode("Function", BOLD_YELLOW);

  this->indent(4);
  this->dumpNullable(func->receiver.get());
  this->dumpList("ParameterList", BOLD_HIGH_INTENSITY_YELLOW, &func->params);
  this->dumpList("ParameterList", BOLD_HIGH_INTENSITY_YELLOW, &func->ret_params);
  this->dumpNullable(func->body.get());
  this->unindent();
}

void ASTDumper::visit(const Parameter *param) {
  this->dumpNode("Parameter", BOLD_HIGH_INTENSITY_YELLOW);

  this->indent(1);
  this->visit(param->type_name.get());
  this->unindent();
}

void ASTDumper::visit(const Variable *var) {
  this->dumpNode("Variable", BOLD_YELLOW);

  this->indent(1);
  this->dumpNullable(var->type_name.get());
  this->unindent();
}

void ASTDumper::visit(const Struct *struct_) {
  this->dumpNode("Struct", BOLD_YELLOW);

  this->indent(1);
  this->dumpList("StructFieldList", BOLD_HIGH_INTENSITY_YELLOW, &struct_->fields);
  this->unindent();
}

void ASTDumper::visit(const StructField *field) {
  this->dumpNode("StructField", BOLD_YELLOW);

  this->indent(1);
  this->visit(field->type_name.get());
  this->unindent();
}

void ASTDumper::visit(const Variant *variant) {
  this->dumpNode("Variant", BOLD_YELLOW);
}

void ASTDumper::visit(const VariantAlternative *alternative) {
  this->dumpNode("VariantAlternative", BOLD_YELLOW);
}

void ASTDumper::visit(const Enum *enum_) {
  this->dumpNode("Enum", BOLD_YELLOW);
}

void ASTDumper::visit(const EnumValue *value) {
  this->dumpNode("EnumValue", BOLD_YELLOW);
}

void ASTDumper::visit(const TypeAlias *type_alias) {
  this->dumpNode("TypeAlias", BOLD_YELLOW);
}

void ASTDumper::beginTypeName(const TypeName *type_name, std::string_view node_kind) {
  this->printIndentation();
  this->withColor(BOLD_PURPLE) << node_kind;
  this->withColor(BOLD_BLACK) << " " << "type_name->range.toReadableString()";
}

void ASTDumper::visit(const TypeName *type_name) {
  switch (type_name->tag) {
  case TypeName::Tag::ElementaryTypeName:    return this->visit(type_name->toElementaryTypeName());
  case TypeName::Tag::PointerTypeName:       return this->visit(type_name->toPointerTypeName());
  case TypeName::Tag::ArrayTypeName:         return this->visit(type_name->toArrayTypeName());
  case TypeName::Tag::SliceTypeName:         return this->visit(type_name->toSliceTypeName());
  case TypeName::Tag::IdentifierTypeName:    return this->visit(type_name->toIdentifierTypeName());
  case TypeName::Tag::ParenthesizedTypeName: return this->visit(type_name->toParenthesizedTypeName());
  }

  assert(false);
}

void ASTDumper::visit(const ElementaryTypeName *type_name) {
  this->beginTypeName(type_name, "ElementaryTypeName");
  this->withColor(BOLD_CYAN) << " " << keywordToString(type_name->keyword) << "\n";
}

void ASTDumper::visit(const PointerTypeName *type_name) {
  this->beginTypeName(type_name, "PointerTypeName");
  this->withColor(BOLD_CYAN) << " " << (type_name->is_owning ? "^" : "*") << "\n";

  this->indent(1);
  this->visit(type_name->pointee.get());
  this->unindent();
}

void ASTDumper::visit(const ArrayTypeName *type_name) {
  this->beginTypeName(type_name, "ArrayTypeName");
  this->os << "\n";

  this->indent(2);
  this->dumpNullable(type_name->length.get());
  this->visit(type_name->element.get());
  this->unindent();
}

void ASTDumper::visit(const SliceTypeName *type_name) {
  this->beginTypeName(type_name, "SliceTypeName");
  this->os << "\n";

  this->indent(1);
  this->visit(type_name->element.get());
  this->unindent();
}

void ASTDumper::visit(const IdentifierTypeName *type_name) {
  this->beginTypeName(type_name, "IdentifierTypeName");
  this->withColor(BOLD_CYAN) << " " << type_name->name << "\n";
}

void ASTDumper::visit(const ParenthesizedTypeName *type_name) {
  this->beginTypeName(type_name, "ParenthesizedTypeName");
  this->os << "\n";

  this->indent(1);
  this->visit(type_name->type_name.get());
  this->unindent();
}

void ASTDumper::beginStmt(const Stmt *stmt, std::string_view node_kind) {
  this->printIndentation();
  this->withColor(BOLD_BLUE) << node_kind;
  this->withColor(BOLD_HIGH_INTENSITY_BLACK) << " " << "stmt->range.toReadableString()";
}

void ASTDumper::visit(const Stmt *stmt) {
  switch (stmt->tag) {
  case Stmt::Tag::BlockStmt:       return this->visit(stmt->toBlockStmt());
  case Stmt::Tag::IfStmt:          return this->visit(stmt->toIfStmt());
  case Stmt::Tag::ForStmt:         return this->visit(stmt->toForStmt());
  case Stmt::Tag::WhileStmt:       return this->visit(stmt->toWhileStmt());
  case Stmt::Tag::LabeledStmt:     return this->visit(stmt->toLabeledStmt());
  case Stmt::Tag::SwitchStmt:      return this->visit(stmt->toSwitchStmt());
  case Stmt::Tag::FallthroughStmt: return this->visit(stmt->toFallthroughStmt());
  case Stmt::Tag::ExprStmt:        return this->visit(stmt->toExprStmt());
  case Stmt::Tag::SkipStmt:        return this->visit(stmt->toSkipStmt());
  case Stmt::Tag::BreakStmt:       return this->visit(stmt->toBreakStmt());
  case Stmt::Tag::ContinueStmt:    return this->visit(stmt->toContinueStmt());
  case Stmt::Tag::VarDeclStmt:     return this->visit(stmt->toVarDeclStmt());
  case Stmt::Tag::ReturnStmt:      return this->visit(stmt->toReturnStmt());
  }

  assert(false);
}

void ASTDumper::visit(const BlockStmt *stmt) {
  this->beginStmt(stmt, "BlockStmt");
  this->os << "\n";

  this->indent(1);
  auto stmts_view = view(stmt->stmts);
  this->dumpList("StmtList", BOLD_HIGH_INTENSITY_BLUE, &stmts_view);
  this->unindent();
}

void ASTDumper::visit(const IfStmt *stmt) {
  this->beginStmt(stmt, "IfStmt");
  this->os << "\n";

  this->indent(3);
  this->visit(stmt->cond.get());
  this->visit(stmt->then.get());
  this->dumpNullable(stmt->else_.get());
  this->unindent();
}

void ASTDumper::visit(const ForStmt *stmt) {
  this->beginStmt(stmt, "ForStmt");
  this->os << "\n";

  this->indent(4);
  this->dumpNullable(stmt->init.get());
  this->dumpNullable(stmt->cond.get());
  this->dumpNullable(stmt->step.get());
  this->dumpNullable(stmt->body.get());
  this->unindent();
}

void ASTDumper::visit(const WhileStmt *stmt) {
  this->beginStmt(stmt, "WhileStmt");
  this->os << "\n";

  this->indent(2);
  this->visit(stmt->cond.get());
  this->visit(stmt->body.get());
  this->unindent();
}

void ASTDumper::visit(const LabeledStmt *stmt) {
  this->beginStmt(stmt, "LabeledStmt");
  this->os << "\n";

  this->indent(1);
  this->visit(stmt->stmt.get());
  this->unindent();
}

void ASTDumper::visit(const SwitchStmt *stmt) {
  this->beginStmt(stmt, "SwitchStmt");
  this->os << "\n";

  this->indent(2);
  this->dumpNullable(stmt->cond.get());
  this->dumpList("SwitchClauseList", BOLD_HIGH_INTENSITY_BLUE, &stmt->clauses);
  this->unindent();
}

void ASTDumper::visit(const SwitchClause *clause) {
  this->dumpNode("SwitchClause", BOLD_BLUE);

  this->indent(2);
  this->dumpNullable(clause->label.get());
  auto body_view = view(clause->body);
  this->dumpList("StmtList", BOLD_HIGH_INTENSITY_BLUE, &body_view);
  this->unindent();
}

void ASTDumper::visit(const FallthroughStmt *stmt) {
  this->beginStmt(stmt, "FallthroughStmt");
  this->os << "\n";
}

void ASTDumper::visit(const ExprStmt *stmt) {
  this->beginStmt(stmt, "ExprStmt");
  this->os << "\n";

  this->indent(1);
  this->visit(stmt->expr.get());
  this->unindent();
}

void ASTDumper::visit(const SkipStmt *stmt) {
  this->beginStmt(stmt, "SkipStmt");
  this->os << "\n";
}

void ASTDumper::visit(const BreakStmt *stmt) {
  this->beginStmt(stmt, "BreakStmt");

  if (stmt->label.has_value())
    this->withColor(BOLD_CYAN) << " " << stmt->label.value();

  this->os << "\n";
}

void ASTDumper::visit(const ContinueStmt *stmt) {
  this->beginStmt(stmt, "ContinueStmt");

  if (stmt->label.has_value())
    this->withColor(BOLD_CYAN) << " " << stmt->label.value();

  this->os << "\n";
}

void ASTDumper::visit(const VarDeclStmt *stmt) {
  this->beginStmt(stmt, "VarDeclStmt");
  this->os << "\n";

  this->indent(2);
  auto vars_view = view(stmt->vars);
  this->dumpList("VarList", BOLD_HIGH_INTENSITY_YELLOW, &vars_view);
  this->dumpNullable(stmt->init.get());
  this->unindent();
}

void ASTDumper::visit(const ReturnStmt *stmt) {
  this->beginStmt(stmt, "ReturnStmt");
  this->os << "\n";

  this->indent(1);
  this->dumpNullable(stmt->expr.get());
  this->unindent();
}

void ASTDumper::beginExpr(const Expr *expr, std::string_view node_kind) {
  this->printIndentation();
  this->withColor(BOLD_GREEN) << node_kind;
  this->withColor(BOLD_HIGH_INTENSITY_BLACK) << " " << "expr->range.toReadableString()";

  if (expr->isAnnotated() && expr->isTyped() && expr->getType() != nullptr)
    this->withColor(BOLD_PURPLE) << " '" << expr->getType()->toString() << "'";
}

void ASTDumper::visit(const Expr *expr) {
  switch (expr->tag) {
  case Expr::Tag::AssignmentExpr:       return this->visit(expr->toAssignmentExpr());
  case Expr::Tag::UnaryExpr:            return this->visit(expr->toUnaryExpr());
  case Expr::Tag::BinaryExpr:           return this->visit(expr->toBinaryExpr());
  case Expr::Tag::CallExpr:             return this->visit(expr->toCallExpr());
  case Expr::Tag::IndexAccessExpr:      return this->visit(expr->toIndexAccessExpr());
  case Expr::Tag::MemberAccessExpr:     return this->visit(expr->toMemberAccessExpr());
  case Expr::Tag::VariantAccessExpr:    return this->visit(expr->toVariantAccessExpr());
  case Expr::Tag::TypeTestExpr:         return this->visit(expr->toTypeTestExpr());
  case Expr::Tag::TypeCastExpr:         return this->visit(expr->toTypeCastExpr());
  case Expr::Tag::IdentifierExpr:       return this->visit(expr->toIdentifierExpr());
  case Expr::Tag::ParenExpr:            return this->visit(expr->toParenExpr());
  case Expr::Tag::ListExpr:             return this->visit(expr->toListExpr());
  case Expr::Tag::CompositeLiteralExpr: return this->visit(expr->toCompositeLiteralExpr());
  case Expr::Tag::NewExpr:              return this->visit(expr->toNewExpr());
  case Expr::Tag::TypeNameExpr:         return this->visit(expr->toTypeNameExpr());
  case Expr::Tag::IntLiteralExpr:       return this->visit(expr->toIntLiteralExpr());
  case Expr::Tag::BoolLiteralExpr:      return this->visit(expr->toBoolLiteralExpr());
  case Expr::Tag::NullExpr:             return this->visit(expr->toNullExpr());
  case Expr::Tag::StringLiteralExpr:    return this->visit(expr->toStringLiteralExpr());
  case Expr::Tag::CharLiteralExpr:      return this->visit(expr->toCharLiteralExpr());
  }

  assert(false);
}

void ASTDumper::visit(const AssignmentExpr *expr) {
  this->beginExpr(expr, "AssignmentExpr");
  this->withColor(BOLD_CYAN) << " " << assignmentOpToString(expr->op) << "\n";

  this->indent(2);
  this->visit(expr->left.get());
  this->visit(expr->right.get());
  this->unindent();
}

void ASTDumper::visit(const UnaryExpr *expr) {
  this->beginExpr(expr, "UnaryExpr");
  this->withColor(BOLD_CYAN) << " " << unaryOpToString(expr->op) << "\n";

  this->indent(1);
  this->visit(expr->operand.get());
  this->unindent();
}

void ASTDumper::visit(const BinaryExpr *expr) {
  this->beginExpr(expr, "BinaryExpr");
  this->withColor(BOLD_CYAN) << " " << binaryOpToString(expr->op) << "\n";

  this->indent(2);
  this->visit(expr->left.get());
  this->visit(expr->right.get());
  this->unindent();
}

void ASTDumper::visit(const CallExpr *expr) {
  this->beginExpr(expr, "CallExpr");
  this->os << "\n";

  this->indent(1);  // 2
  this->visit(expr->callee.get());
  // std::vector<Expr *> args_view = view(expr->arguments);
  // this->dumpList("ExprList", BOLD_HIGH_INTENSITY_GREEN, &args_view);
  this->unindent();
}

void ASTDumper::visit(const IndexAccessExpr *expr) {
  this->beginExpr(expr, "IndexAccessExpr");
  this->os << "\n";

  this->indent(2);
  this->visit(expr->base.get());
  this->visit(expr->index.get());
  this->unindent();
}

void ASTDumper::visit(const MemberAccessExpr *expr) {
  this->beginExpr(expr, "MemberAccessExpr");
  this->withColor(BOLD_CYAN) << " " << expr->member_name
                             << " " << (expr->is_arrow ? "->" : ".") << "\n";

  this->indent(1);
  this->visit(expr->base.get());
  this->unindent();
}

void ASTDumper::visit(const VariantAccessExpr *expr) {
  this->beginExpr(expr, "VariantAccessExpr");
  this->os << "\n";

  this->indent(2);
  this->visit(expr->base.get());
  this->visit(expr->type_name.get());
  this->unindent();
}

void ASTDumper::visit(const TypeTestExpr *expr) {
  this->beginExpr(expr, "TypeTestExpr");
  this->os << "\n";

  this->indent(2);
  this->visit(expr->operand.get());
  this->visit(expr->type_name.get());
  this->unindent();
}

void ASTDumper::visit(const TypeCastExpr *expr) {
  this->beginExpr(expr, "TypeCastExpr");
  this->os << "\n";

  this->indent(2);
  this->visit(expr->operand.get());
  this->visit(expr->type_name.get());
  this->unindent();
}

void ASTDumper::visit(const IdentifierExpr *expr) {
  this->beginExpr(expr, "IdentifierExpr");
  this->withColor(BOLD_CYAN) << " " << expr->name << "\n";
}

void ASTDumper::visit(const ParenExpr *expr) {
  this->beginExpr(expr, "ParenExpr");
  this->os << "\n";

  this->indent(1);
  this->visit(expr->expr.get());
  this->unindent();
}

void ASTDumper::visit(const ListExpr *expr) {
  this->beginExpr(expr, "ListExpr");
  this->os << "\n";

  this->indent(1);
  std::vector<Expr *> components_view = view(expr->components);
  this->dumpList("ExprList", BOLD_HIGH_INTENSITY_GREEN, &components_view);
  this->unindent();
}

void ASTDumper::visit(const CompositeLiteralExpr *expr) {
  this->beginExpr(expr, "CompositeLiteralExpr");
  this->os << "\n";

  this->indent(1);
  std::vector<Expr *> components_view = view(expr->components);
  this->dumpList("ExprList", BOLD_HIGH_INTENSITY_GREEN, &components_view);
  this->unindent();
}

void ASTDumper::visit(const NewExpr *expr) {
  this->beginExpr(expr, "NewExpr");
  this->os << "\n";

  this->indent(1);
  this->visit(expr->type_name.get());
  this->unindent();
}

void ASTDumper::visit(const TypeNameExpr *expr) {
  this->beginExpr(expr, "TypeNameExpr");
  this->os << "\n";

  this->indent(1);
  this->visit(expr->type_name.get());
  this->unindent();
}

void ASTDumper::visit(const IntLiteralExpr *expr) {
  this->beginExpr(expr, "IntLiteralExpr");
  this->withColor(BOLD_CYAN) << " " << expr->value << "\n";
}

void ASTDumper::visit(const BoolLiteralExpr *expr) {
  this->beginExpr(expr, "BoolLiteralExpr");
  this->withColor(BOLD_CYAN) << " " << expr->value << "\n";
}

void ASTDumper::visit(const NullExpr *expr) {
  this->beginExpr(expr, "NullExpr");
  this->os << "\n";
}

void ASTDumper::visit(const StringLiteralExpr *expr) {
  this->beginExpr(expr, "StringLiteralExpr");
  this->withColor(BOLD_CYAN) << " " << stringValueToLiteral(expr->value) << "\n";
}

void ASTDumper::visit(const CharLiteralExpr *expr) {
  this->beginExpr(expr, "CharLiteralExpr");
  this->withColor(BOLD_CYAN) << " " << charValueToLiteral(expr->value) << "\n";
}
