#include "cfg-builder.hpp"

#include <optional>
#include <string>
#include <vector>

#include "../frontend/constant-provider.hpp"
#include "../frontend/type.hpp"
#include "../frontend/type-provider.hpp"

cfg::CFG CFGBuilder::build() {
  cfg::BasicBlock *entry_bb = this->newBlock();
  this->beginBlock(entry_bb);

  this->beginScope();

  for (Parameter &param : this->func->params)
    this->addInScope(ScopeEntry(cfg::Storage(&param)));

  this->visit(this->func->body.get());

  cfg::BasicBlock *body_cont_bb = this->insert_bb;

  this->endScope(body_cont_bb != nullptr);
  assert(this->scope_stack.empty());

  if (body_cont_bb != nullptr) {
    if (this->func->ret_params.empty()) {
      this->build(new cfg::ReturnTerm(nullptr));
    } else {
      this->diagnostics->warning(func->loc.value(), "control reaches end of non-void function");
      this->insert_bb->setTerminator(new cfg::PanicTerm(func->loc.value(), "control reached end of non-void function"));
    }
  }

  return cfg::CFG(std::move(this->blocks));
}

void CFGBuilder::beginBlock(cfg::BasicBlock *block) {
  this->blocks.push_back(block);
  this->insert_bb = block;
}

void CFGBuilder::beginScope() {
  this->scope_stack.push_back(Scope());
}

void CFGBuilder::endScope(bool cleanup) {
  assert(!this->scope_stack.empty());

  if (cleanup)
    this->cleanupScope(this->scope_stack.back());

  this->scope_stack.pop_back();
}

void CFGBuilder::cleanupScope(const Scope &scope) {
  for (auto it = scope.entries.rbegin(), end = scope.entries.rend(); it != end; ++it) {
    const ScopeEntry &entry = *it;

    if (std::holds_alternative<cfg::Storage>(entry))
      this->build(new cfg::EndStorageInst(std::get<cfg::Storage>(entry)));
    else
      this->build(new cfg::DropValInst(std::get<cfg::Register *>(entry)));
  }
}

void CFGBuilder::cleanupUpToIndex(unsigned target_index) {
  assert(target_index <= this->scope_stack.size() - 1);

  for (unsigned i = this->scope_stack.size() - 1; true; --i) {
    this->cleanupScope(this->scope_stack[i]);
    if (i == target_index)
      break;
  }
}

void CFGBuilder::addInScope(ScopeEntry entry) {
  assert(!this->scope_stack.empty());
  this->scope_stack.back().entries.push_back(entry);
}

void CFGBuilder::build(cfg::Instruction *inst) {
  assert(this->insert_bb != nullptr);
  this->insert_bb->insts.push_back(inst);
}

cfg::RegisterValue *CFGBuilder::build(cfg::ValueInst *inst) {
  assert(this->insert_bb != nullptr);
  this->insert_bb->insts.push_back(inst);
  return new cfg::RegisterValue(inst->reg);
}

void CFGBuilder::build(cfg::Terminator *term) {
  this->insert_bb->setTerminator(term);
  this->insert_bb = nullptr;
}

void CFGBuilder::warnUnreachableStmt(const Stmt *stmt) {
  if (stmt->isBlockStmt())
    stmt = stmt->toBlockStmt()->stmts.front().get();

  this->diagnostics->warning(stmt->range, "unreachable statement");
}

void CFGBuilder::visit(const Stmt *stmt) {
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

void CFGBuilder::visit(const BlockStmt *stmt) {
  this->beginScope();
  this->lowerStmtList(view(stmt->stmts));
  // No insert block -> cleanup is already emitted
  this->endScope(this->insert_bb != nullptr);
}

void CFGBuilder::lowerStmtList(const std::vector<Stmt *> &stmts) {
  assert(this->insert_bb != nullptr);

  for (const Stmt *stmt : stmts) {
    if (this->insert_bb == nullptr) {
      this->warnUnreachableStmt(stmt);
      break;
    }

    this->visit(stmt);
  }
}

void CFGBuilder::visit(const IfStmt *stmt) {
  if (stmt->cond->isConstant())
    return this->lowerConstantIfStmt(stmt);

  this->beginScope();
  cfg::Value *cond = this->lowerToValue(stmt->cond.get(), nullptr);
  this->endScope(true);

  cfg::BasicBlock *cond_cont_bb = this->insert_bb;

  cfg::BasicBlock *then_bb = this->newBlock();
  this->beginBlock(then_bb);
  this->visit(stmt->then.get());
  cfg::BasicBlock *then_cont_bb = this->insert_bb;

  if (stmt->else_ != nullptr) {
    cfg::BasicBlock *else_bb = this->newBlock();

    cond_cont_bb->setTerminator(new cfg::IfTerm(cond, then_bb, else_bb));

    this->beginBlock(else_bb);
    this->visit(stmt->else_.get());
    cfg::BasicBlock *else_cont_bb = this->insert_bb;

    // If both branches have no continuation block, return without appending an endif block
    // (because nobody is going to jump to it).
    if (then_cont_bb == nullptr && else_cont_bb == nullptr) {
      this->insert_bb = nullptr;
      return;
    }

    cfg::BasicBlock *merge_bb = this->newBlock();

    // Must jump only if the block doesn't terminate.
    if (then_cont_bb != nullptr)
      then_cont_bb->setTerminator(new cfg::GotoTerm(merge_bb));

    // Must jump only if the block doesn't terminate.
    if (else_cont_bb != nullptr)
      else_cont_bb->setTerminator(new cfg::GotoTerm(merge_bb));

    this->beginBlock(merge_bb);
  } else {
    cfg::BasicBlock *merge_bb = this->newBlock();

    cond_cont_bb->setTerminator(new cfg::IfTerm(cond, then_bb, merge_bb));

    if (then_cont_bb != nullptr)
      then_cont_bb->setTerminator(new cfg::GotoTerm(merge_bb));

    this->beginBlock(merge_bb);
  }
}

void CFGBuilder::lowerConstantIfStmt(const IfStmt *stmt) {
  bool taken = stmt->cond->getConstant()->toBoolConstant()->value;

  if (taken) {
    this->visit(stmt->then.get());

    if (stmt->else_ != nullptr)
      this->warnUnreachableStmt(stmt->else_.get());
  } else {
    this->warnUnreachableStmt(stmt->then.get());

    if (stmt->else_ != nullptr)
      this->visit(stmt->else_.get());
  }
}

void CFGBuilder::visit(const ForStmt *stmt) {
  this->lower(stmt, {});
}

void CFGBuilder::lower(const ForStmt *stmt, std::optional<std::string> label) {
  if (stmt->cond == nullptr || stmt->cond->isConstant())
    return this->lowerConstantForStmt(stmt, std::move(label));

  unsigned break_index = this->scope_stack.size() - 1;
  this->beginScope();
  unsigned continue_index = this->scope_stack.size() - 1;

  if (stmt->init != nullptr)
    this->visit(stmt->init.get());

  cfg::BasicBlock *cond_bb = this->newBlock();
  this->insert_bb->setTerminator(new cfg::GotoTerm(cond_bb));

  this->beginBlock(cond_bb);
  cfg::Value *cond = this->lowerToValue(stmt->cond.get(), nullptr);

  cfg::BasicBlock *body_bb = this->newBlock();
  cfg::BasicBlock *merge_bb = this->newBlock();

  this->insert_bb->setTerminator(new cfg::IfTerm(cond, body_bb, merge_bb));

  cfg::BasicBlock *continue_target = (stmt->step == nullptr ? cond_bb : nullptr);

  this->loop_stack.emplace_back(break_index, continue_index, merge_bb, continue_target, std::move(label));

  this->beginBlock(body_bb);
  this->visit(stmt->body.get());
  cfg::BasicBlock *body_cont_bb = this->insert_bb;

  cfg::BasicBlock *loop_stack_continue_target = this->loop_stack.back().continue_target;

  this->loop_stack.pop_back();

  if (stmt->step != nullptr) {
    // Create the 'step' if it exists and the body did not terminate or it did terminate but a nested 'continue' targetted this loop.
    // In which case, the 'step' is not reachable from the body but reachable from the nested 'continue' statement.
    if (body_cont_bb != nullptr || loop_stack_continue_target != nullptr) {
      cfg::BasicBlock *step_bb = (loop_stack_continue_target != nullptr
                                  ? loop_stack_continue_target
                                  : this->newBlock());

      if (body_cont_bb != nullptr)
        body_cont_bb->setTerminator(new cfg::GotoTerm(step_bb));

      this->beginBlock(step_bb);
      this->lowerExprStmt(stmt->step.get());
      assert(this->insert_bb != nullptr);
      this->insert_bb->setTerminator(new cfg::GotoTerm(cond_bb));
    } else {
      this->diagnostics->warning(stmt->step->range, "unreachable statement");
    }
  } else if (body_cont_bb != nullptr) {
    // No step, body jump to cond_bb
    body_cont_bb->setTerminator(new cfg::GotoTerm(cond_bb));
  }

  this->beginBlock(merge_bb);
  this->endScope(true);
}

void CFGBuilder::lowerConstantForStmt(const ForStmt *stmt, std::optional<std::string> label) {
  bool taken = stmt->cond == nullptr || stmt->cond->getConstant()->toBoolConstant()->value;

  // for (XXX; false; XXX)
  if (!taken) {
    if (stmt->init != nullptr) {
      this->beginScope();
      this->visit(stmt->init.get());
    }

    if (stmt->step != nullptr)
      this->diagnostics->warning(stmt->step->range, "unreachable statement");

    this->warnUnreachableStmt(stmt->body.get());

    if (stmt->init != nullptr)
      this->endScope(true);
    return;
  }

  // for (XXX; true; XXX)
  unsigned break_index = this->scope_stack.size() - 1;
  this->beginScope();
  unsigned continue_index = this->scope_stack.size() - 1;

  if (stmt->init != nullptr)
    this->visit(stmt->init.get());

  cfg::BasicBlock *body_bb = this->newBlock();
  this->insert_bb->setTerminator(new cfg::GotoTerm(body_bb));

  // If there is no step, the continue target is the body block.
  // Otherwise, it is created lazily. It is not known at this point whether the 'step' is needed.
  cfg::BasicBlock *continue_target = (stmt->step == nullptr ? body_bb : nullptr);

  this->loop_stack.emplace_back(break_index, continue_index, nullptr, continue_target, std::move(label));

  this->beginBlock(body_bb);
  this->visit(stmt->body.get());
  cfg::BasicBlock *body_cont_bb = this->insert_bb;

  cfg::BasicBlock *loop_stack_break_target = this->loop_stack.back().break_target;
  cfg::BasicBlock *loop_stack_continue_target = this->loop_stack.back().continue_target;

  this->loop_stack.pop_back();

  if (stmt->step != nullptr) {
    // Create the 'step' if it exists and the body did not terminate or it did terminate but a nested 'continue' targetted this loop.
    // In which case, the 'step' is not reachable from the body but reachable from the nested 'continue' statement.
    if (body_cont_bb != nullptr || loop_stack_continue_target != nullptr) {
      cfg::BasicBlock *step_bb = (loop_stack_continue_target != nullptr
                                  ? loop_stack_continue_target
                                  : this->newBlock());

      // Jump from the body only if it did not terminate
      if (body_cont_bb != nullptr)
        body_cont_bb->setTerminator(new cfg::GotoTerm(step_bb));

      this->beginBlock(step_bb);
      this->lowerExprStmt(stmt->step.get());
      this->insert_bb->setTerminator(new cfg::GotoTerm(body_bb));
    } else {
      this->diagnostics->warning(stmt->step->range, "unreachable statement");
    }
  }

  if (loop_stack_break_target != nullptr) {
    cfg::BasicBlock *merge_bb = loop_stack_break_target;
    this->beginBlock(merge_bb);
    this->endScope(true);
  } else {
    this->endScope(false);
    this->insert_bb = nullptr;
  }
}

void CFGBuilder::visit(const WhileStmt *stmt) {
  this->lower(stmt, {});
}

void CFGBuilder::lower(const WhileStmt *stmt, std::optional<std::string> label) {
  if (stmt->cond->isConstant())
    return this->lowerConstantWhileStmt(stmt, std::move(label));

  unsigned scope_index = this->scope_stack.size() - 1;

  cfg::BasicBlock *cond_bb = this->newBlock();
  this->insert_bb->setTerminator(new cfg::GotoTerm(cond_bb));

  this->beginBlock(cond_bb);
  cfg::Value *cond = this->lowerToValue(stmt->cond.get(), nullptr);
  cfg::BasicBlock *cond_cont_bb = this->insert_bb;

  cfg::BasicBlock *body_bb = this->newBlock();
  cfg::BasicBlock *merge_bb = this->newBlock();

  this->loop_stack.emplace_back(scope_index, scope_index, merge_bb, cond_bb, std::move(label));

  this->beginBlock(body_bb);
  this->visit(stmt->body.get());
  cfg::BasicBlock *body_cont_bb = this->insert_bb;

  this->loop_stack.pop_back();

  if (body_cont_bb != nullptr)
    body_cont_bb->setTerminator(new cfg::GotoTerm(cond_bb));

  cond_cont_bb->setTerminator(new cfg::IfTerm(cond, body_bb, merge_bb));
  this->beginBlock(merge_bb);
}

void CFGBuilder::lowerConstantWhileStmt(const WhileStmt *stmt, std::optional<std::string> label) {
  bool taken = stmt->cond->getConstant()->toBoolConstant()->value;

  if (!taken) {
    this->warnUnreachableStmt(stmt->body.get());
    return;
  }

  unsigned scope_index = this->scope_stack.size() - 1;

  cfg::BasicBlock *body_bb = this->newBlock();
  this->insert_bb->setTerminator(new cfg::GotoTerm(body_bb));

  // Pass null to the break target, built lazily if somebody wants to jump to it
  this->loop_stack.emplace_back(scope_index, scope_index, nullptr, body_bb, std::move(label));

  this->beginBlock(body_bb);
  this->visit(stmt->body.get());
  cfg::BasicBlock *body_cont_bb = this->insert_bb;

  if (body_cont_bb != nullptr)
    body_cont_bb->setTerminator(new cfg::GotoTerm(body_bb));

  cfg::BasicBlock *merge_bb = this->loop_stack.back().break_target;

  if (merge_bb != nullptr)
    this->beginBlock(merge_bb);
  else
    this->insert_bb = nullptr;
}

void CFGBuilder::visit(const LabeledStmt *stmt) {
  if (stmt->stmt->isForStmt())
    return this->lower(stmt->stmt->toForStmt(), stmt->label);

  if (stmt->stmt->isWhileStmt())
    return this->lower(stmt->stmt->toWhileStmt(), stmt->label);

  assert(false);
}

void CFGBuilder::visit(const SwitchStmt *stmt) {
  if (stmt->cond == nullptr)
    return this->lowerExpressionlessSwitch(stmt);

  if (stmt->cond->getType()->isVariantType())
    return this->lowerVariantSwitch(stmt);

  return this->lowerValueSwitch(stmt);
}

void CFGBuilder::lowerValueSwitch(const SwitchStmt *stmt) {
  cfg::Value *cond = this->lowerToValue(stmt->cond.get(), nullptr);
  cfg::BasicBlock *cond_cont_bb = this->insert_bb;

  const std::vector<SwitchClause> &clauses = stmt->clauses;

  if (clauses.empty())
    return;

  std::unordered_map<const SwitchClause *, cfg::BasicBlock *> clause_targets = this->createSwitchClauseTargets(clauses);

  std::vector<cfg::SwitchClause> cfg_clauses;
  cfg::BasicBlock *cfg_default_target = nullptr;

  for (const SwitchClause &clause : clauses) {
    cfg::BasicBlock *target = clause_targets.at(&clause);

    if (clause.isDefault()) {
      cfg_default_target = target;
    } else {
      const Type *target_type = stmt->cond->getType();
      cfg::Value *label = this->lowerToValue(clause.label.get(), target_type);

      cfg_clauses.push_back(cfg::SwitchClause(label, target));
    }
  }

  bool is_exhaustive = (stmt->cond->getType()->isEnumType()
                        && (stmt->cond->getType()->toEnumType()->getEnum()->values.size() == cfg_clauses.size()));

  cfg::BasicBlock *merge_bb;

  if (!is_exhaustive && cfg_default_target == nullptr) {
    merge_bb = this->newBlock();
    cfg_default_target = merge_bb;
  } else {
    merge_bb = nullptr;
  }

  cond_cont_bb->setTerminator(new cfg::SwitchTerm(cond, cfg_clauses, cfg_default_target, stmt->range.getBegin()));
  this->lowerSwitchClauses(clauses, clause_targets, merge_bb);
}

void CFGBuilder::lowerVariantSwitch(const SwitchStmt *stmt) {
  cfg::Place *cond = this->lowerToPlace(stmt->cond.get());
  cfg::BasicBlock *cond_cont_bb = this->insert_bb;

  const std::vector<SwitchClause> &clauses = stmt->clauses;

  if (clauses.empty())
    return;

  std::unordered_map<const SwitchClause *, cfg::BasicBlock *> clause_targets = this->createSwitchClauseTargets(clauses);

  std::vector<cfg::VariantSwitchClause> cfg_clauses;
  cfg::BasicBlock *cfg_default_target = nullptr;

  for (const SwitchClause &clause : clauses) {
    cfg::BasicBlock *target = clause_targets.at(&clause);

    if (clause.isDefault()) {
      cfg_default_target = target;
    } else {
      assert(clause.alternative != nullptr);
      cfg_clauses.push_back(cfg::VariantSwitchClause(clause.alternative, target));
    }
  }

  size_t alt_count = stmt->cond->getType()->toVariantType()->getVariant()->alternatives.size();
  bool is_exhaustive = cfg_clauses.size() == alt_count;

  cfg::BasicBlock *merge_bb;

  if (!is_exhaustive && cfg_default_target == nullptr) {
    merge_bb = this->newBlock();
    cfg_default_target = merge_bb;
  } else {
    merge_bb = nullptr;
  }

  cond_cont_bb->setTerminator(new cfg::VariantSwitchTerm(cond, cfg_clauses, cfg_default_target, stmt->range.getBegin()));
  this->lowerSwitchClauses(clauses, clause_targets, merge_bb);
}

std::unordered_map<const SwitchClause *, cfg::BasicBlock *> CFGBuilder::createSwitchClauseTargets(const std::vector<SwitchClause> &clauses) {
  std::unordered_map<const SwitchClause *, cfg::BasicBlock *> clause_targets;

  for (size_t i = 0, n = clauses.size(); i < n;) {
    cfg::BasicBlock *target = this->newBlock();

    // Assign the same target to one or more consecutive clauses,
    // until and including the first clause that has a body.
    while (true) {
      const SwitchClause &clause = clauses.at(i);
      clause_targets.insert({&clause, target});

      bool has_body = !clause.body.empty();
      ++i;

      if (has_body)
        break;

      assert(i < clauses.size());
    }
  }

  return clause_targets;
}

void CFGBuilder::lowerSwitchClauses(const std::vector<SwitchClause> &clauses,
                                    const std::unordered_map<const SwitchClause *, cfg::BasicBlock *> &clause_targets,
                                    cfg::BasicBlock *merge_bb) {
  for (const SwitchClause &clause : clauses) {
    if (clause.body.empty())
      continue;

    cfg::BasicBlock *target = clause_targets.at(&clause);

    this->beginBlock(target);
    this->lowerStmtList(view(clause.body));
    cfg::BasicBlock *body_cont_bb = this->insert_bb;

    if (body_cont_bb != nullptr) {
      if (merge_bb == nullptr)
        merge_bb = this->newBlock();

      body_cont_bb->setTerminator(new cfg::GotoTerm(merge_bb));
    }
  }

  if (merge_bb != nullptr)
    this->beginBlock(merge_bb);
  else
    this->insert_bb = nullptr;
}

void CFGBuilder::lowerExpressionlessSwitch(const SwitchStmt *stmt) {
  assert(false && "unimplemented");
}

void CFGBuilder::visit(const FallthroughStmt *) {
  assert(false && "handled in SwitchStmt lowering");
}

void CFGBuilder::visit(const ExprStmt *stmt) {
  this->lowerExprStmt(stmt->expr.get());
}

void CFGBuilder::lowerExprStmt(const Expr *expr) {
  if (!expr->isAnnotated())
    return this->lowerAsStatement(expr);

  this->beginScope();

  ExprResult res = this->visit(expr);

  if (res.is<cfg::Value *>() && expr->getType()->requiresDrop(*this->type_infos)) {
    cfg::Value *value = res.get<cfg::Value *>();

    if (expr->getType()->isPointerType()) {
      this->build(new cfg::DropValInst(value->toRegisterValue()->reg));
    } else {
      cfg::Temporary *temp = this->newTemporary(expr->getType());
      this->addInScope(ScopeEntry(cfg::Storage(temp)));

      cfg::Place *place = new cfg::Place(cfg::PlaceBase(temp));
      this->build(new cfg::BeginStorageInst(cfg::Storage(temp)));
      this->build(new cfg::InitPlaceInst(place, value));
    }
  } else {
    // TODO: Cleanup 'res'
  }

  this->endScope(true);
}

void CFGBuilder::visit(const SkipStmt *) {
  // Nothing to do
  return;
}

void CFGBuilder::visit(const BreakStmt *stmt) {
  LoopContext *ctx = this->findLoopContext(stmt->label);

  if (ctx->break_index + 1 <= this->scope_stack.size() - 1)
    this->cleanupUpToIndex(ctx->break_index + 1);

  if (ctx->break_target == nullptr)
    ctx->break_target = this->newBlock();

  this->build(new cfg::GotoTerm(ctx->break_target));
}

void CFGBuilder::visit(const ContinueStmt *stmt) {
  LoopContext *ctx = this->findLoopContext(stmt->label);

  if (ctx->continue_index + 1 <= this->scope_stack.size() - 1)
    this->cleanupUpToIndex(ctx->continue_index);

  if (ctx->continue_target == nullptr)
    ctx->continue_target = this->newBlock();

  this->build(new cfg::GotoTerm(ctx->continue_target));
}

CFGBuilder::LoopContext *CFGBuilder::findLoopContext(const std::optional<std::string> &target_label) {
  if (!target_label.has_value()) {
    assert(!this->loop_stack.empty());
    return &this->loop_stack.back();
  }

  for (auto it = this->loop_stack.rbegin(); it != this->loop_stack.rend(); ++it) {
    const std::optional<std::string> &loop_label = it->label;
    if (loop_label.has_value() && loop_label.value() == target_label.value())
      return &*it;
  }

  assert(false);
}

void CFGBuilder::visit(const VarDeclStmt *stmt) {
  const std::vector<std::unique_ptr<Variable>> &vars = stmt->vars;

  for (const std::unique_ptr<Variable> &var : vars) {
    this->build(new cfg::BeginStorageInst(cfg::Storage(var.get())));
    this->addInScope(ScopeEntry(cfg::Storage(var.get())));
  }

  if (stmt->init == nullptr)
    return;

  if (vars.size() == 1 || stmt->init->isListExpr()) {
    std::vector<Expr *> components = stmt->init->extractComponents();

    for (size_t i = 0, n = components.size(); i < n; ++i) {
      cfg::Value *value = this->lowerToValue(components[i], vars[i].get()->type);
      cfg::Place *place = new cfg::Place(cfg::PlaceBase(vars[i].get()));

      this->build(new cfg::InitPlaceInst(place, value));
    }
  } else {
    std::vector<cfg::Place *> targets;
    targets.reserve(vars.size() - 1);

    for (size_t i = 1, n = vars.size(); i < n; ++i)
      targets.push_back(new cfg::Place(cfg::PlaceBase(vars[i].get())));

    cfg::Value *first_ret = this->lowerCallForTargets(stmt->init->toCallExpr(), targets);

    if (!first_ret->getType()->isEqualTo(vars.front()->type))
      first_ret = this->convert(first_ret, first_ret->getType(), vars.front()->type);

    cfg::Place *first_var = new cfg::Place(cfg::PlaceBase(vars.front().get()));
    this->build(new cfg::InitPlaceInst(first_var, first_ret));
  }
}

void CFGBuilder::visit(const ReturnStmt *stmt) {
  cfg::Value *value;

  std::vector<Parameter> &ret_params = this->func->ret_params;

  if (ret_params.empty()) {
    if (stmt->expr != nullptr && !stmt->expr->isListExpr())
      this->lowerAsStatement(stmt->expr.get());
    value = nullptr;
  } else if (ret_params.size() == 1 || stmt->expr->isListExpr()) {
    std::vector<Expr *> components = stmt->expr->extractComponents();

    value = this->lowerToValue(components.front(), ret_params.front().type);

    for (size_t i = 1, n = components.size(); i < n; ++i) {
      cfg::Value *ret_val = this->lowerToValue(components[i], ret_params[i].type);
      cfg::Place *place = new cfg::Place(cfg::PlaceBase(&ret_params[i]));

      this->build(new cfg::InitPlaceInst(place, ret_val));
    }
  } else {
    std::vector<cfg::Place *> targets;
    targets.reserve(ret_params.size() - 1);

    for (size_t i = 1, n = ret_params.size(); i < n; ++i)
      targets.push_back(new cfg::Place(cfg::PlaceBase(&ret_params[i])));

    cfg::Value *first_ret = this->lowerCallForTargets(stmt->expr->toCallExpr(), targets);

    const Type *target = ret_params.front().type;
    value = this->convert(first_ret, stmt->expr->toCallExpr()->arguments.front().value->getType(), target);
  }

  this->cleanupUpToIndex(0);
  this->build(new cfg::ReturnTerm(value));
}

cfg::Place *CFGBuilder::lowerToPlace(const Expr *expr) {
  ExprResult res = this->visit(expr);
  return res.get<cfg::Place *>();
}

cfg::Value *CFGBuilder::lowerToValue(const Expr *expr, const Type *target) {
  // Case 1) Returned cfg::Value is a constant, when 'expr' is a constant and the target is not a variant type
  if (expr->isConstant() && !(target != nullptr && target->isVariantType())) {
    const Constant *c = expr->getConstant();

    if (target == nullptr || expr->getType()->isEqualTo(target))
      return new cfg::ConstantValue(c, expr->getType());
    else
      return new cfg::ConstantValue(c->convert(expr->getType(), target), target);
  }

  ExprResult res = (expr->isConstant()
                    ? new cfg::ConstantValue(expr->getConstant(), expr->getType())
                    : this->visit(expr));

  // Case 2) No type conversion
  if (target == nullptr || expr->getType()->isEqualTo(target)) {
    if (res.is<cfg::Value *>())
      return res.get<cfg::Value *>();

    cfg::Place *place = res.get<cfg::Place *>();

    if (expr->getType()->requiresTake(*this->type_infos))
      return this->build(new cfg::TakeInst(this->newRegister(expr->getType()), place));
    else
      return this->build(new cfg::LoadInst(this->newRegister(expr->getType()), place));
  }

  // Case 3) Implicit conversion
  return this->convert(res, expr->getType(), target);
}

cfg::Value *CFGBuilder::convert(ExprResult res, const Type *src, const Type *target) {
  assert(!src->isEqualTo(target));

  // Conversions that require a place, in which case the conversion never consumes
  if ((src->isStringType() && target->isStrType())
      || (src->isArrayType() && target->isSliceType())) {
    cfg::Place *src_place;

    if (res.is<cfg::Place *>()) {
      src_place = res.get<cfg::Place *>();
    } else {
      cfg::Temporary *temp = this->newTemporary(src);
      this->addInScope(ScopeEntry(cfg::Storage(temp)));

      src_place = new cfg::Place(cfg::PlaceBase(temp));
      this->build(new cfg::BeginStorageInst(cfg::Storage(temp)));
      this->build(new cfg::InitPlaceInst(src_place, res.get<cfg::Value *>()));
    }

    return this->build(new cfg::PlaceCastInst(this->newRegister(target), src_place, target));
  }

  if (target->isVariantType()) {
    if (res.is<cfg::Value *>()) {
      return this->build(new cfg::ValueCastInst(this->newRegister(src), res.get<cfg::Value *>(), target));
    } else if (src->requiresTake(*this->type_infos)) {
      cfg::RegisterValue *taken = this->build(new cfg::TakeInst(this->newRegister(src), res.get<cfg::Place *>()));
      return this->build(new cfg::ValueCastInst(this->newRegister(target), taken, target));
    } else {
      cfg::RegisterValue *loaded = this->build(new cfg::LoadInst(this->newRegister(src), res.get<cfg::Place *>()));
      return this->build(new cfg::ValueCastInst(this->newRegister(target), loaded, target));
    }
  }

  // ^T to *T
  if (src->isPointerType() && target->isPointerType()) {
    if (res.is<cfg::Value *>()) {
      cfg::Value *value = res.get<cfg::Value *>();
      assert(value->isRegisterValue());
      cfg::Register *reg = value->toRegisterValue()->reg;
      this->addInScope(ScopeEntry(reg));
      return this->build(new cfg::ValueCastInst(this->newRegister(target), value, target));
    } else {
      // Only requires a load, Load(^T) returns *T
      return this->build(new cfg::LoadInst(this->newRegister(target), res.get<cfg::Place *>()));
    }
  }

  // Remaining conversions that require a value
  if (res.is<cfg::Place *>()) {
    cfg::Value *loaded = this->build(new cfg::LoadInst(this->newRegister(src), res.get<cfg::Place *>()));
    return this->build(new cfg::ValueCastInst(this->newRegister(target), loaded, target));
  } else {
    return this->build(new cfg::ValueCastInst(this->newRegister(target), res.get<cfg::Value *>(), target));
  }
}

cfg::Place *CFGBuilder::lowerToPlaceOrPlaceBase(const Expr *expr) {
  if (expr->getType()->isPointerType()) {
    const Type *pointee = expr->getType()->toPointerType()->getPointee();
    const PointerType *target = TypeProvider::Pointer(pointee, false);

    cfg::Value *value = this->lowerToValue(expr, target);
    return new cfg::Place(cfg::PlaceBase(cfg::Deref(value)));
  } else {
    return this->lowerToPlace(expr);
  }
}

void CFGBuilder::lowerAsStatement(const Expr *expr) {
  switch (expr->tag) {
  case Expr::Tag::AssignmentExpr: {
    this->lowerAssignmentAsStmt(expr->toAssignmentExpr());
    break;
  }
  case Expr::Tag::CallExpr: {
    this->lowerCallAsStmt(expr->toCallExpr());
    break;
  }
  default:
    assert(false);
  }
}

CFGBuilder::ExprResult CFGBuilder::visit(const Expr *expr) {
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

CFGBuilder::ExprResult CFGBuilder::visit(const AssignmentExpr *) {
  assert(false);
}

void CFGBuilder::lowerAssignmentAsStmt(const AssignmentExpr *expr) {
  std::vector<Expr *> left_components = expr->left->extractComponents();

  if (left_components.size() == 1 || expr->right->isListExpr()) {
    std::vector<Expr *> right_components = expr->right->extractComponents();

    for (size_t i = 0, n = left_components.size(); i < n; ++i) {
      cfg::Value *value = this->lowerToValue(right_components[i], left_components[i]->getType());
      ExprResult res = this->visit(left_components[i]);
      cfg::Place *place = res.get<cfg::Place *>();

      if (expr->op == AssignmentOp::Initialize)
        this->build(new cfg::InitPlaceInst(place, value));
      else
        this->build(new cfg::AssignPlaceInst(place, value));
    }
  } else {
    std::vector<cfg::Place *> targets;
    targets.reserve(left_components.size() - 1);

    for (size_t i = 1, n = left_components.size(); i < n; ++i) {
      ExprResult res = this->visit(left_components[i]);
      cfg::Place *place = res.get<cfg::Place *>();
      targets.push_back(place);
    }

    cfg::Value *first_ret = this->lowerCallForTargets(expr->right->toCallExpr(), targets);

    const Type *target = left_components.front()->getType();

    if (!first_ret->getType()->isEqualTo(target))
      first_ret = this->convert(first_ret, first_ret->getType(), target);

    assert(false);
  }
}

CFGBuilder::ExprResult CFGBuilder::visit(const UnaryExpr *expr) {
  const Expr *operand = expr->operand.get();

  switch (expr->op) {
  case UnaryOp::Plus:
    return this->lowerToValue(operand, nullptr);
  case UnaryOp::Indirection: {
    const PointerType *ptr_type = operand->getType()->toPointerType();
    const PointerType *target = TypeProvider::Pointer(ptr_type->getPointee(), false);

    cfg::Value *value = this->lowerToValue(operand, target);
    return new cfg::Place(cfg::PlaceBase(cfg::Deref(value)));
  }
  case UnaryOp::AddressOf: {
    ExprResult res = this->visit(operand);
    cfg::Place *place = res.get<cfg::Place *>();
    return this->build(new cfg::AddressOfInst(this->newRegister(expr->getType()), place));
  }
  case UnaryOp::Minus:
  case UnaryOp::Not:
  case UnaryOp::BitNot: {
    cfg::Value *cfg_oper = this->lowerToValue(operand, nullptr);
    return this->build(new cfg::UnaryOpInst(this->newRegister(expr->getType()), this->unary_ops_map.at(expr->op), cfg_oper));
  }
  }

  assert(false);
}

CFGBuilder::ExprResult CFGBuilder::visit(const BinaryExpr *expr) {
  if (expr->op == BinaryOp::And)
    return this->lowerAnd(expr);

  if (expr->op == BinaryOp::Or)
    return this->lowerOr(expr);

  // str x str -> string
  if (expr->op == BinaryOp::Add
      && expr->left->getType()->isImplicitlyConvertibleTo(TypeProvider::Str())
      && expr->right->getType()->isImplicitlyConvertibleTo(TypeProvider::Str())) {
    const StrType *target = TypeProvider::Str();
    cfg::Value *cfg_left = this->lowerToValue(expr->left.get(), target);
    cfg::Value *cfg_right = this->lowerToValue(expr->right.get(), target);
    return this->build(new cfg::BinaryOpInst(this->newRegister(expr->getType()), cfg::BinaryOp::Add, cfg_left, cfg_right));
  }

  // *T x *T -> bool
  if ((expr->op == BinaryOp::EQ || expr->op == BinaryOp::NE)
      && expr->left->getType()->isPointerType()
      && expr->right->getType()->isPointerType()) {
    const Type *pointee = expr->left->getType()->toPointerType()->getPointee();
    const PointerType *target = TypeProvider::Pointer(pointee, false);

    cfg::BinaryOp cfg_op = this->binary_ops_map.find(expr->op)->second;
    cfg::Value *cfg_left = this->lowerToValue(expr->left.get(), target);
    cfg::Value *cfg_right = this->lowerToValue(expr->right.get(), target);
    return this->build(new cfg::BinaryOpInst(this->newRegister(expr->getType()), cfg_op, cfg_left, cfg_right));
  }

  // str x str -> bool
  if ((expr->op == BinaryOp::EQ || expr->op == BinaryOp::NE)
      && expr->left->getType()->isImplicitlyConvertibleTo(TypeProvider::Str())
      && expr->right->getType()->isImplicitlyConvertibleTo(TypeProvider::Str())) {
    const StrType *target = TypeProvider::Str();
    cfg::Value *cfg_left = this->lowerToValue(expr->left.get(), target);
    cfg::Value *cfg_right = this->lowerToValue(expr->right.get(), target);
    cfg::RegisterValue *res = this->build(new cfg::BinaryOpInst(this->newRegister(expr->getType()), cfg::BinaryOp::EQ, cfg_left, cfg_right));

    if (expr->op == BinaryOp::EQ)
      return res;
    else
      return this->build(new cfg::UnaryOpInst(this->newRegister(TypeProvider::Bool()), cfg::UnaryOp::Not, res));
  }

  cfg::BinaryOp cfg_op = this->binary_ops_map.find(expr->op)->second;
  cfg::Value *cfg_left = this->lowerToValue(expr->left.get(), nullptr);
  cfg::Value *cfg_right = this->lowerToValue(expr->right.get(), nullptr);
  return this->build(new cfg::BinaryOpInst(this->newRegister(expr->getType()), cfg_op, cfg_left, cfg_right));
}

cfg::Value *CFGBuilder::lowerAnd(const BinaryExpr *expr) {
  cfg::Temporary *temp = this->newTemporary(TypeProvider::Bool());
  this->build(new cfg::BeginStorageInst(cfg::Storage(temp)));

  cfg::Value *left = this->lowerToValue(expr->left.get(), nullptr);
  cfg::BasicBlock *left_bb_end = this->insert_bb;

  cfg::BasicBlock *then_bb = this->newBlock();
  cfg::BasicBlock *else_bb = this->newBlock();
  cfg::BasicBlock *merge_bb = this->newBlock();

  left_bb_end->setTerminator(new cfg::IfTerm(left, then_bb, else_bb));

  this->beginBlock(then_bb);
  cfg::Value *right = this->lowerToValue(expr->right.get(), nullptr);
  cfg::BasicBlock *right_bb_end = this->insert_bb;
  this->build(new cfg::InitPlaceInst(new cfg::Place(cfg::PlaceBase(temp)), right));
  right_bb_end->setTerminator(new cfg::GotoTerm(merge_bb));

  this->beginBlock(else_bb);
  cfg::ConstantValue *false_ = new cfg::ConstantValue(ConstantProvider::Bool(false), TypeProvider::Bool());
  this->build(new cfg::InitPlaceInst(new cfg::Place(cfg::PlaceBase(temp)), false_));
  else_bb->setTerminator(new cfg::GotoTerm(merge_bb));

  this->beginBlock(merge_bb);

  return this->build(new cfg::LoadInst(this->newRegister(temp->type), new cfg::Place(cfg::PlaceBase(temp))));
}

cfg::Value *CFGBuilder::lowerOr(const BinaryExpr *expr) {
  cfg::Temporary *temp = this->newTemporary(TypeProvider::Bool());
  this->build(new cfg::BeginStorageInst(cfg::Storage(temp)));

  cfg::Value *left = this->lowerToValue(expr->left.get(), nullptr);
  cfg::BasicBlock *left_bb_end = this->insert_bb;

  cfg::BasicBlock *then_bb = this->newBlock();
  cfg::BasicBlock *else_bb = this->newBlock();
  cfg::BasicBlock *merge_bb = this->newBlock();

  left_bb_end->setTerminator(new cfg::IfTerm(left, then_bb, else_bb));

  this->beginBlock(then_bb);
  cfg::ConstantValue *true_ = new cfg::ConstantValue(ConstantProvider::Bool(true), TypeProvider::Bool());
  this->build(new cfg::InitPlaceInst(new cfg::Place(cfg::PlaceBase(temp)), true_));
  then_bb->setTerminator(new cfg::GotoTerm(merge_bb));

  this->beginBlock(else_bb);
  cfg::Value *right = this->lowerToValue(expr->right.get(), nullptr);
  cfg::BasicBlock *right_bb_end = this->insert_bb;
  this->build(new cfg::InitPlaceInst(new cfg::Place(cfg::PlaceBase(temp)), right));
  right_bb_end->setTerminator(new cfg::GotoTerm(merge_bb));

  this->beginBlock(merge_bb);

  return this->build(new cfg::LoadInst(this->newRegister(temp->type), new cfg::Place(cfg::PlaceBase(temp))));
}

CFGBuilder::ExprResult CFGBuilder::visit(const CallExpr *expr) {
  const CallKind &call_kind = expr->call_kind.value();

  switch (call_kind.getTag()) {
  case CallKind::Tag::FunctionCall: {
    cfg::Call call = this->lowerCall(expr, {});
    return this->build(new cfg::CallInst(this->newRegister(call.func->ret_params.front().type), std::move(call)));
  }
  case CallKind::Tag::TypeCall: {
    const TypeCall &type_call = call_kind.get<TypeCall>();
    const Type *type = type_call.type;

    if (type_call.make_func == nullptr || type_call.make_func->isSelfMake()) {
      cfg::Value *value = this->lowerToValue(expr->arguments.front().value.get(), type);
      return expr->isNew() ? this->build(new cfg::NewInst(this->newRegister(TypeProvider::Pointer(type, true)), type, value)) : value;
    } else {
      cfg::Call call = this->lowerCall(expr, {});
      cfg::RegisterValue *call_value = this->build(new cfg::CallInst(this->newRegister(call.func->ret_params.front().type), std::move(call)));
      return expr->isNew() ? this->build(new cfg::NewInst(this->newRegister(TypeProvider::Pointer(type, true)), type, call_value)) : call_value;
    }
  }
  }

  assert(false);
}

cfg::Call CFGBuilder::lowerCall(const CallExpr *expr, std::vector<cfg::Place *> out_args) {
  const CallKind &call_kind = expr->call_kind.value();

  Function *func = (call_kind.is<FunctionCall>()
                    ? call_kind.get<FunctionCall>().func
                    : call_kind.get<TypeCall>().make_func);

  std::vector<cfg::Value *> args;

  if (call_kind.is<FunctionCall>() && call_kind.get<FunctionCall>().is_bound_method_call) {
    const Expr *receiver_arg = expr->callee->toMemberAccessExpr()->base.get();
    args.push_back(this->lowerReceiver(receiver_arg, func->receiver->type));
  }

  const std::vector<CallArg> &arguments = expr->arguments;

  if (func->receiver != nullptr && !call_kind.get<FunctionCall>().is_bound_method_call) {
    // Unbound method call
    args.push_back(this->lowerToValue(arguments.front().value.get(), func->receiver->type));

    for (size_t i = 1, n = arguments.size(); i < n; ++i) {
      const Type *target = func->params.at(i - 1).type;
      args.push_back(this->lowerToValue(arguments[i].value.get(), target));
    }
  } else {
    for (size_t i = 0, n = arguments.size(); i < n; ++i) {
      const Type *target = func->params.at(i).type;
      args.push_back(this->lowerToValue(arguments[i].value.get(), target));
    }
  }

  return cfg::Call(func, std::move(args), std::move(out_args), expr->range.getBegin());
}

cfg::Value *CFGBuilder::lowerCallForTargets(const CallExpr *expr, const std::vector<cfg::Place *> &targets) {
  const std::vector<Parameter> &ret_params = expr->call_kind.value().get<FunctionCall>().func->ret_params;
  assert(ret_params.size() == targets.size() + 1);

  std::vector<cfg::Place *> out_args;
  out_args.reserve(targets.size());

  std::vector<TempFixup> fixups;

  for (size_t i = 0, n = targets.size(); i < n; ++i) {
    if (ret_params.at(i + 1).type->isEqualTo(targets[i]->getType())) {
      out_args.push_back(targets[i]);
    } else {
      cfg::Temporary *temp = this->newTemporary(ret_params.at(i + 1).type);
      this->build(new cfg::BeginStorageInst(cfg::Storage(temp)));

      out_args.push_back(new cfg::Place(cfg::PlaceBase(temp)));
      fixups.push_back({targets[i], temp});
    }
  }

  cfg::Call call = this->lowerCall(expr, out_args);
  cfg::RegisterValue *first_ret = this->build(new cfg::CallInst(this->newRegister(ret_params.front().type), std::move(call)));

  for (const TempFixup &fx : fixups) {
    cfg::Value *converted = this->convert(new cfg::Place(cfg::PlaceBase(fx.temp)), fx.temp->type, fx.target->getType());
    this->build(new cfg::InitPlaceInst(fx.target, converted));
  }

  return first_ret;
}

cfg::Value *CFGBuilder::lowerReceiver(const Expr *receiver_arg, const Type *receiver) {
  if (receiver->isPointerType() && !receiver_arg->getType()->isPointerType()) {
    cfg::Place *cfg_receiver_arg = this->lowerToPlace(receiver_arg);
    return this->build(new cfg::AddressOfInst(this->newRegister(receiver), cfg_receiver_arg));
  } else if (!receiver->isPointerType() && receiver_arg->getType()->isPointerType()) {
    const PointerType *target = TypeProvider::Pointer(receiver_arg->getType()->toPointerType()->getPointee(), false);
    cfg::Value *cfg_receiver_arg = this->lowerToValue(receiver_arg, target);
    cfg::Place *place = new cfg::Place(cfg::PlaceBase(cfg::Deref(cfg_receiver_arg)));
    return this->build(new cfg::LoadInst(this->newRegister(receiver), place));
  } else {
    return this->lowerToValue(receiver_arg, receiver);
  }
}

void CFGBuilder::lowerCallAsStmt(const CallExpr *expr) {
  const std::vector<Parameter> &ret_params = expr->call_kind.value().get<FunctionCall>().func->ret_params;
  assert(ret_params.size() != 1);

  if (ret_params.empty()) {
    cfg::Call call = this->lowerCall(expr->toCallExpr(), {});

    if (call.func->is_no_return) {
      cfg::CallTerm *term = new cfg::CallTerm(std::move(call));
      return this->build(term);
    } else {
      cfg::VoidCallInst *inst = new cfg::VoidCallInst(std::move(call));
      return this->build(inst);
    }
  } else {
    std::vector<cfg::Place *> out_args;

    this->beginScope();

    for (size_t i = 1, n = ret_params.size(); i < n; ++i) {
      cfg::Temporary *temp = this->newTemporary(ret_params[i].type);
      this->addInScope(ScopeEntry(cfg::Storage(temp)));
      this->build(new cfg::BeginStorageInst(cfg::Storage(temp)));
      out_args.push_back(new cfg::Place(cfg::PlaceBase(temp)));
    }

    cfg::Call call = this->lowerCall(expr, out_args);
    cfg::RegisterValue *value = this->build(new cfg::CallInst(this->newRegister(ret_params.front().type), std::move(call)));

    if (ret_params.front().type->requiresDrop(*this->type_infos)) {
      if (ret_params.front().type->isPointerType()) {
        this->build(new cfg::DropValInst(value->toRegisterValue()->reg));
      } else {
        cfg::Temporary *temp = this->newTemporary(ret_params.front().type);
        this->addInScope(ScopeEntry(cfg::Storage(temp)));

        cfg::Place *place = new cfg::Place(cfg::PlaceBase(temp));
        this->build(new cfg::BeginStorageInst(cfg::Storage(temp)));
        this->build(new cfg::InitPlaceInst(place, value));
      }
    } else {
      // delete value;
    }

    this->endScope(true);
    return;
  }
}

CFGBuilder::ExprResult CFGBuilder::visit(const IndexAccessExpr *expr) {
  cfg::Place *base = this->lowerToPlace(expr->base.get());

  const Type *target = TypeProvider::UInt64();
  cfg::Value *cfg_index = this->lowerToValue(expr->index.get(), target);

  base->projections.push_back(cfg::Projection(cfg_index));
  return base;
}

CFGBuilder::ExprResult CFGBuilder::visit(const MemberAccessExpr *expr) {
  cfg::Place *base = this->lowerToPlaceOrPlaceBase(expr->base.get());

  if (expr->field != nullptr) {
    base->projections.push_back(cfg::Projection(expr->field));
    return base;
  } else {
    assert(expr->property != nullptr);
    return this->build(new cfg::PropertyAccessInst(this->newRegister(expr->getType()), base, expr->property));
  }
}

CFGBuilder::ExprResult CFGBuilder::visit(const VariantAccessExpr *expr) {
  cfg::Place *base = this->lowerToPlaceOrPlaceBase(expr->base.get());
  base->projections.push_back(cfg::Projection(expr->alternative));
  return base;
}

CFGBuilder::ExprResult CFGBuilder::visit(const TypeTestExpr *expr) {
  cfg::Place *place = this->lowerToPlace(expr->operand.get());
  return this->build(new cfg::TypeTestInst(this->newRegister(expr->getType()), place, expr->alternative, expr->is_negated));
}

CFGBuilder::ExprResult CFGBuilder::visit(const TypeCastExpr *expr) {
  const Type *target = expr->target;

  if (expr->operand->getType()->isImplicitlyConvertibleTo(target)) {
    return this->lowerToValue(expr->operand.get(), target);
  } else {
    // All explicit conversions that are not implicit too operate on values
    cfg::Value *value = this->lowerToValue(expr->operand.get(), nullptr);
    return this->build(new cfg::ValueCastInst(this->newRegister(target), value, target));
  }
}

CFGBuilder::ExprResult CFGBuilder::visit(const IdentifierExpr *expr) {
  if (expr->var != nullptr) {
    return new cfg::Place(cfg::PlaceBase(expr->var));
  } else {
    assert(expr->param != nullptr);
    return new cfg::Place(cfg::PlaceBase(expr->param));
  }
}

CFGBuilder::ExprResult CFGBuilder::visit(const ParenExpr *expr) {
  return this->visit(expr->expr.get());
}

CFGBuilder::ExprResult CFGBuilder::visit(const ListExpr *) {
  assert(false);
}

CFGBuilder::ExprResult CFGBuilder::visit(const CompositeLiteralExpr *expr) {
  assert(expr->isTyped());
  std::vector<Expr *> components = view(expr->components);

  std::vector<cfg::Value *> cfg_components;
  cfg_components.reserve(components.size());

  if (expr->getType()->isArrayType()) {
    const Type *target = expr->getType()->toArrayType()->getElement();

    for (const Expr *component : components)
      cfg_components.push_back(this->lowerToValue(component, target));
  } else if (expr->getType()->isSliceType()) {
    assert(components.empty());
  } else {
    assert(expr->getType()->isStructType());
    Struct *struct_ = expr->getType()->toStructType()->getStruct();

    for (size_t i = 0, n = components.size(); i < n; ++i) {
      const Type *target = struct_->fields[i].type;
      cfg_components.push_back(this->lowerToValue(components[i], target));
    }
  }

  return this->build(new cfg::CompositeLiteralInst(this->newRegister(expr->getType()), std::move(cfg_components)));
}

CFGBuilder::ExprResult CFGBuilder::visit(const NewExpr *) {
  assert(false);
}

CFGBuilder::ExprResult CFGBuilder::visit(const TypeNameExpr *) {
  assert(false);
}

CFGBuilder::ExprResult CFGBuilder::visit(const IntLiteralExpr *) {
  assert(false && "lowered as a constant by CFGBuilder::lowerToValue");
}

CFGBuilder::ExprResult CFGBuilder::visit(const BoolLiteralExpr *) {
  assert(false && "lowered as a constant by CFGBuilder::lowerToValue");
}

CFGBuilder::ExprResult CFGBuilder::visit(const NullExpr *) {
  assert(false && "lowered as a constant by CFGBuilder::lowerToValue");
}

CFGBuilder::ExprResult CFGBuilder::visit(const StringLiteralExpr *) {
  assert(false && "lowered as a constant by CFGBuilder::lowerToValue");
}

CFGBuilder::ExprResult CFGBuilder::visit(const CharLiteralExpr *) {
  assert(false && "lowered as a constant by CFGBuilder::lowerToValue");
}
