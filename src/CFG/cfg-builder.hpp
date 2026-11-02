#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../frontend/acl.hpp"
#include "cfg.hpp"

struct Constant;
class Diagnostics;

class CFGBuilder {
 public:
  CFGBuilder(Function *func, std::unordered_map<const Type *, TypeInfo> *type_infos, Diagnostics *diagnostics)
      : func(func), type_infos(type_infos), diagnostics(diagnostics) { }

  cfg::CFG build();

 private:
  void warnUnreachableStmt(const Stmt *stmt);
  void visit(const Stmt *stmt);
  void visit(const BlockStmt *stmt);
  void lowerStmtList(const std::vector<Stmt *> &stmts);
  void visit(const IfStmt *stmt);
  void lowerConstantIfStmt(const IfStmt *stmt);
  void visit(const ForStmt *stmt);
  void lower(const ForStmt *stmt, std::optional<std::string> label);
  void lowerConstantForStmt(const ForStmt *stmt, std::optional<std::string> label);
  void visit(const WhileStmt *stmt);
  void lower(const WhileStmt *stmt, std::optional<std::string> label);
  void lowerConstantWhileStmt(const WhileStmt *stmt, std::optional<std::string> label);
  void visit(const LabeledStmt *stmt);
  void visit(const SwitchStmt *stmt);
  void lowerValueSwitch(const SwitchStmt *stmt);
  void lowerVariantSwitch(const SwitchStmt *stmt);
  std::unordered_map<const SwitchClause *, cfg::BasicBlock *> createSwitchClauseTargets(const std::vector<SwitchClause> &clauses);
  void lowerSwitchClauses(const std::vector<SwitchClause> &clauses,
                          const std::unordered_map<const SwitchClause *, cfg::BasicBlock *> &clause_targets,
                          cfg::BasicBlock *merge_bb);
  void lowerExpressionlessSwitch(const SwitchStmt *stmt);
  void visit(const FallthroughStmt *stmt);
  void visit(const ExprStmt *stmt);
  void lowerExprStmt(const Expr *expr);
  void visit(const SkipStmt *stmt);
  void visit(const BreakStmt *stmt);
  void visit(const ContinueStmt *stmt);
  struct LoopContext;
  LoopContext *findLoopContext(const std::optional<std::string> &target_label);
  void visit(const VarDeclStmt *stmt);
  void visit(const ReturnStmt *stmt);

  struct ExprResult : public variant<ExprResult, cfg::Place *, cfg::Value *> {
    enum class Tag { Place, Value };
    using variant::variant;
  };

  cfg::Place *lowerToPlace(const Expr *expr);
  cfg::Value *lowerToValue(const Expr *expr, const Type *target);
  cfg::Value *convert(ExprResult res, const Type *src, const Type *target);
  cfg::Place *lowerToPlaceOrPlaceBase(const Expr *expr);
  // Lower an expression which is not a pure expression (does not have a type)
  void lowerAsStatement(const Expr *expr);

  ExprResult visit(const Expr *expr);
  ExprResult visit(const AssignmentExpr *expr);
  void lowerAssignmentAsStmt(const AssignmentExpr *expr);
  ExprResult visit(const UnaryExpr *expr);
  ExprResult visit(const BinaryExpr *expr);
  cfg::Value *lowerAnd(const BinaryExpr *expr);
  cfg::Value *lowerOr(const BinaryExpr *expr);
  ExprResult visit(const CallExpr *expr);
  cfg::Call lowerCall(const CallExpr *expr, std::vector<cfg::Place *> out_args);
  cfg::Value *lowerCallForTargets(const CallExpr *expr, const std::vector<cfg::Place *> &targets);
  cfg::Value *lowerReceiver(const Expr *receiver_arg, const Type *receiver);
  void lowerCallAsStmt(const CallExpr *expr);
  ExprResult visit(const IndexAccessExpr *expr);
  ExprResult visit(const MemberAccessExpr *expr);
  ExprResult visit(const VariantAccessExpr *expr);
  ExprResult visit(const TypeTestExpr *expr);
  ExprResult visit(const TypeCastExpr *expr);
  ExprResult visit(const IdentifierExpr *expr);
  ExprResult visit(const ParenExpr *expr);
  ExprResult visit(const ListExpr *expr);
  ExprResult visit(const CompositeLiteralExpr *expr);
  ExprResult visit(const NewExpr *expr);
  ExprResult visit(const TypeNameExpr *expr);
  ExprResult visit(const IntLiteralExpr *expr);
  ExprResult visit(const BoolLiteralExpr *expr);
  ExprResult visit(const NullExpr *expr);
  ExprResult visit(const StringLiteralExpr *expr);
  ExprResult visit(const CharLiteralExpr *expr);

  struct LoopContext {
    LoopContext(unsigned break_index,
                unsigned continue_index,
                cfg::BasicBlock *break_target,
                cfg::BasicBlock *continue_target,
                std::optional<std::string> label)
        : break_index(break_index), continue_index(continue_index), break_target(break_target), continue_target(continue_target), label(std::move(label)) { }
    unsigned break_index;
    unsigned continue_index;
    cfg::BasicBlock *break_target;
    cfg::BasicBlock *continue_target;
    std::optional<std::string> label;
  };

  struct TempFixup {
    cfg::Place *target;
    cfg::Temporary *temp;
  };

  using ScopeEntry = std::variant<cfg::Storage, cfg::Register *>;

  struct Scope {
    std::vector<ScopeEntry> entries;
  };

  cfg::BasicBlock *newBlock() {
    return new cfg::BasicBlock(this->next_block_id++);
  }

  cfg::Temporary *newTemporary(const Type *type) {
    return new cfg::Temporary(this->next_temp_id++, type);
  }

  cfg::Register *newRegister(const Type *type) {
    return new cfg::Register(this->next_reg_id++, type);
  }

  void beginScope();
  void endScope(bool cleanup);
  void cleanupScope(const Scope &scope);
  void cleanupUpToIndex(unsigned target_index);
  void addInScope(ScopeEntry entry);

  void beginBlock(cfg::BasicBlock *block);
  void build(cfg::Instruction *inst);
  // Build a value instruction and returns a reference to the instruction's register
  cfg::RegisterValue *build(cfg::ValueInst *inst);
  void build(cfg::Terminator *term);

  Function *func;
  std::unordered_map<const Type *, TypeInfo> *type_infos;
  Diagnostics *diagnostics;

  std::vector<cfg::BasicBlock *> blocks;

  std::vector<LoopContext> loop_stack;

  // counters
  unsigned next_block_id = 0;
  unsigned next_temp_id = 0;
  unsigned next_reg_id = 0;

  cfg::BasicBlock *insert_bb = nullptr;

  std::vector<Scope> scope_stack;

  std::unordered_map<UnaryOp, cfg::UnaryOp> unary_ops_map = {
    { UnaryOp::Minus  , cfg::UnaryOp::Minus  },
    { UnaryOp::Not    , cfg::UnaryOp::Not    },
    { UnaryOp::BitNot , cfg::UnaryOp::BitNot }
  };

  std::unordered_map<BinaryOp, cfg::BinaryOp> binary_ops_map = {
    { BinaryOp::Add,    cfg::BinaryOp::Add    },
    { BinaryOp::Sub,    cfg::BinaryOp::Sub    },
    { BinaryOp::Mul,    cfg::BinaryOp::Mul    },
    { BinaryOp::Div,    cfg::BinaryOp::Div    },
    { BinaryOp::Rem,    cfg::BinaryOp::Rem    },
    { BinaryOp::LT,     cfg::BinaryOp::LT     },
    { BinaryOp::LE,     cfg::BinaryOp::LE     },
    { BinaryOp::GT,     cfg::BinaryOp::GT     },
    { BinaryOp::GE,     cfg::BinaryOp::GE     },
    { BinaryOp::EQ,     cfg::BinaryOp::EQ     },
    { BinaryOp::NE,     cfg::BinaryOp::NE     },
    { BinaryOp::BitAnd, cfg::BinaryOp::BitAnd },
    { BinaryOp::BitOr,  cfg::BinaryOp::BitOr  },
    { BinaryOp::BitXor, cfg::BinaryOp::BitXor },
    { BinaryOp::SHL,    cfg::BinaryOp::SHL    },
    { BinaryOp::SHR,    cfg::BinaryOp::SHR    },
  };
};
