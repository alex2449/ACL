#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "acl.hpp"
#include "constant.hpp"
#include "diagnostics.hpp"
#include "../util/expected.hpp"

class StructType;
class VariantType;

struct SemaResult {
  std::unordered_map<const Type *, TypeInfo> type_infos;
  std::vector<Function *> funcs;

  SemaResult(std::unordered_map<const Type *, TypeInfo> type_infos, std::vector<Function *> funcs)
      : type_infos(std::move(type_infos)), funcs(std::move(funcs)) { }
};

class Sema {
 public:
  Sema(Program *program, Diagnostics *diag) : program(program), diag(diag) { }

  SemaResult process();

 private:
  void initializeTypeInfos();

  void registerSymbols(SourceUnit *unit);
  void bindSymbol(std::string_view name, Symbol sym, Scope *scope, SourceUnit *unit);
  void warnShadowing(std::string_view name, Symbol sym, const std::vector<Symbol> *shadowed);
  bool bindSymbol(std::string_view name, Symbol sym, Bindings *bindings);
  const std::vector<Symbol> *lookupSymbol(std::string_view name, const Scope *scope, SourceUnit *unit);

  struct ImportInfo {
    ImportInfo(const Import *import, const std::vector<Symbol> *syms)
        : import(import), syms(syms) { }
    const Import *import;
    const std::vector<Symbol> *syms;
  };

  void checkImports(SourceUnit *unit);
  void resolveTypeNames(SourceUnit *unit);

  void checkTypes(SourceUnit *unit);
  void checkDependencyCycle(const Type *start, SourceLocation decl_loc);

  const Type *ensureTypeRegistered(const Type *type);

  void registerIntrinsics();
  void completeTypeInfos();
  void collectUserTypeInfo(const Type *type);
  // Populate type info with compiler symbols
  void populateTypeInfo(const Type *type);

  static std::unique_ptr<Function> getSelfMake(const Type *type);
  static std::unique_ptr<Function> getStructMake(const StructType *type);
  static std::unique_ptr<Function> getVariantMake(const VariantType *type, VariantAlternative *alt);
  static std::unique_ptr<Function> getMake(const Type *type, std::vector<Parameter> params);
  static std::unique_ptr<Function> getTake(const Type *type);
  static std::unique_ptr<Function> getDrop(const Type *type);

  void checkOverloads();
  void checkOverloads(Bindings *bindings);
  void checkOverloadSet(const std::vector<Symbol> &syms);
  void checkFunctionBodies(SourceUnit *unit);
  void checkMain();
  void checkUnusedImports();

  struct TypeNameContext {
    TypeNameContext(SourceUnit *unit, Scope *scope) : unit(unit), scope(scope) { }
    SourceUnit *unit;
    Scope *scope;
  };

  const Type *resolve(const TypeName *type_name, TypeNameContext &ctx);
  const Type *resolve(const ElementaryTypeName *type_name, TypeNameContext &ctx);
  const Type *resolve(const PointerTypeName *type_name, TypeNameContext &ctx);
  const Type *resolve(const ArrayTypeName *type_name, TypeNameContext &ctx);
  const Type *resolve(const SliceTypeName *type_name, TypeNameContext &ctx);
  const Type *resolve(const IdentifierTypeName *type_name, TypeNameContext &ctx);
  const Type *resolve(const ParenthesizedTypeName *type_name, TypeNameContext &ctx);

  struct StmtContext {
    StmtContext(SourceUnit *unit, Scope *scope, Function *func, std::vector<const Stmt *> *break_targets)
        : unit(unit), scope(scope), func(func), break_targets(break_targets) { }
    SourceUnit *unit;
    Scope *scope;
    Function *func;
    std::vector<const Stmt *> *break_targets;
  };

  void typeCheck(Stmt *stmt, StmtContext &ctx);
  void typeCheck(BlockStmt *stmt, StmtContext &ctx);
  void typeCheck(IfStmt *stmt, StmtContext &ctx);
  void typeCheck(ForStmt *stmt, StmtContext &ctx);
  void typeCheck(WhileStmt *stmt, StmtContext &ctx);
  void typeCheck(LabeledStmt *stmt, StmtContext &ctx);
  void typeCheck(SwitchStmt *stmt, StmtContext &ctx);
  void typeCheckExpressionlessSwitch(SwitchStmt *stmt, StmtContext &ctx);
  void typeCheckVariantSwitch(SwitchStmt *stmt, StmtContext &ctx, bool seen_default);
  void typeCheckValueSwitch(SwitchStmt *stmt, StmtContext &ctx, bool seen_default);
  void typeCheckInvalidSwitch(SwitchStmt *stmt, StmtContext &ctx);
  void typeCheckSwitchClauseBody(SwitchClause &clause, std::vector<SwitchClause> &clauses, StmtContext &ctx);
  void typeCheck(FallthroughStmt *stmt, StmtContext &ctx);
  void typeCheck(ExprStmt *stmt, StmtContext &ctx);
  void typeCheck(SkipStmt *stmt, StmtContext &ctx);
  void typeCheck(BreakStmt *stmt, StmtContext &ctx);
  void typeCheck(ContinueStmt *stmt, StmtContext &ctx);
  void typeCheckBreakOrContinue(Stmt *stmt, StmtContext &ctx, const std::optional<std::string> &label, std::string_view kind);
  void typeCheck(VarDeclStmt *stmt, StmtContext &ctx);
  void typeCheckVarDecl(VarDeclStmt *stmt, const std::vector<Expr *> &components, StmtContext &ctx);
  void typeCheckVarDeclCall(VarDeclStmt *stmt, const CallExpr *call, StmtContext &ctx);
  void typeCheck(ReturnStmt *stmt, StmtContext &ctx);
  void typeCheckReturn(ReturnStmt *stmt, const std::vector<Expr *> &components, StmtContext &ctx);
  void typeCheckReturnCall(ReturnStmt *stmt, const CallExpr *call, StmtContext &ctx);

  static std::vector<Function *> symbolsToFunctions(const std::vector<Symbol> &syms);

  struct ExprContext {
    ExprContext(SourceUnit *unit, Scope *scope) : unit(unit), scope(scope) { }
    SourceUnit *unit;
    Scope *scope;
  };

  // Returns nullopt if an error was reported, nullptr if no type could be resolved (with no errors), otherwise returns the type
  std::optional<const Type *> tryResolveAsTypeName(const Expr *expr, ExprContext &ctx);

  enum class ExprResult {
    PureStmt,                 // Expression has no value
    Expr,                     // Expression is a pure expr, ExprAnno is set
    MultiReturnFunctionCall,  // Access expr->call_anno.get<FunctionCallAnno>().func
    Error
  };

  bool expectType(Expr *expr, const Type *target, const SourceRange *ctx_range);

  struct ErrorInfo {
    struct NoteInfo {
      DiagSourceInfo src_info;
      std::string message;

      template<typename... Args>
      NoteInfo(DiagSourceInfo src_info, fmt::format_string<Args...> fmt, Args&&... args)
          : src_info(std::move(src_info)), message(fmt::format(fmt, std::forward<Args>(args)...)) { }
    };

    DiagSourceInfo src_info;
    std::string message;
    std::vector<NoteInfo> note_info;

    template<typename... Args>
    ErrorInfo(DiagSourceInfo src_info, fmt::format_string<Args...> fmt, Args&&... args)
        : src_info(std::move(src_info)), message(fmt::format(fmt, std::forward<Args>(args)...)) { }
  };

  enum class ConvRank {
    Exact,
    Implicit,
  };

  struct AssignInfo {
    const Type *target;
    ConvRank rank;
    AssignInfo(const Type *target, ConvRank rank) : target(target), rank(rank) { }
  };

  tl::expected<AssignInfo, ErrorInfo> tryAssignExprTo(const Expr *expr, const Type *target);
  void assignExprTo(Expr *expr, const Type *type);

  void typeCheckNoContext(Expr *expr, ExprContext &ctx);
  bool tryTypeExpr(Expr *expr);
  void typeCheck(Expr *expr, ExprContext &ctx);
  ExprResult resolve(Expr *expr, ExprContext &ctx);

  void typeCheck(AssignmentExpr *expr, ExprContext &ctx);
  ExprResult resolve(AssignmentExpr *expr, ExprContext &ctx);
  ExprResult resolveAssignment(AssignmentExpr *expr, const std::vector<Expr *> &left_components, const std::vector<Expr *> &right_components, ExprContext &ctx);
  ExprResult resolveAssignmentCall(AssignmentExpr *expr, const std::vector<Expr *> &left_components, const CallExpr *call, ExprContext &ctx);
  void typeCheck(UnaryExpr *expr, ExprContext &ctx);
  void typeCheck(BinaryExpr *expr, ExprContext &ctx);
  void finishBinary(BinaryExpr *expr, TypeState type_state);
  tl::expected<const Constant *, ErrorInfo> evalBinary(const BinaryExpr *expr);
  bool checkIntegerConstantFits(const IntegerConstant *constant, const IntegerType *type, SourceRange range);
  void typeCheck(CallExpr *expr, ExprContext &ctx);
  ExprResult resolve(CallExpr *expr, ExprContext &ctx);
  Function *resolveFunctionCall(CallExpr *expr, const std::vector<Function *> *funcs, ExprContext &ctx, bool is_bound);
  Function *resolveFunctionCall(CallExpr *expr, Function *func, ExprContext &ctx, bool is_bound);

  struct OverloadMatch {
    Function *func;
    std::vector<ConvRank> ranks;
    std::vector<const Type *> arg_types;

    OverloadMatch(Function *func) : func(func) { }
    bool beatsAll(const std::vector<OverloadMatch> &matches) const;
    bool dominates(const OverloadMatch &other) const;
  };

  Function *resolveOverloadedFunctionCall(CallExpr *expr, const std::vector<Function *> *funcs, ExprContext &ctx, bool is_bound);
  std::optional<OverloadMatch> tryMatchOverload(Function *func, const std::vector<CallArg> &args, bool is_bound);
  static const OverloadMatch *selectBestMatch(const std::vector<OverloadMatch> &matches);

  void checkFunctionCallNames(CallExpr *expr, Function *func, bool is_bound);
  static ExprResult funcResult(CallExpr *expr, Function *func, bool is_bound_method_call);
  ExprResult resolveMethodCall(CallExpr *expr, ExprContext &ctx, const Type *lookup_type,
                               const MemberAccessExpr *member_access, bool is_bound);
  ExprResult resolveTypeCall(CallExpr *expr, const Type *type, ExprContext &ctx);
  void typeCheck(IndexAccessExpr *expr, ExprContext &ctx);
  void typeCheck(MemberAccessExpr *expr, ExprContext &ctx);
  void typeCheckQualifiedMemberAccess(MemberAccessExpr *expr, ExprContext &ctx, const Type *qualifier);
  void typeCheck(VariantAccessExpr *expr, ExprContext &ctx);
  void typeCheck(TypeTestExpr *expr, ExprContext &ctx);
  void typeCheck(TypeCastExpr *expr, ExprContext &ctx);
  void typeCheck(IdentifierExpr *expr, ExprContext &ctx);
  void typeCheck(ParenExpr *expr, ExprContext &ctx);
  void typeCheck(ListExpr *expr, ExprContext &ctx);
  bool typeCheckListComponents(ListExpr *expr, ExprContext &ctx);
  void typeCheck(CompositeLiteralExpr *expr, ExprContext &ctx);
  void typeCheck(NewExpr *expr, ExprContext &ctx);
  void typeCheck(TypeNameExpr *expr, ExprContext &ctx);
  void typeCheck(IntLiteralExpr *expr, ExprContext &ctx);
  void typeCheck(BoolLiteralExpr *expr, ExprContext &ctx);
  void typeCheck(NullExpr *expr, ExprContext &ctx);
  void typeCheck(StringLiteralExpr *expr, ExprContext &ctx);
  void typeCheck(CharLiteralExpr *expr, ExprContext &ctx);

  Program *program;
  Diagnostics *diag;
  bool type_infos_populated = false;

  std::unordered_map<const Type *, TypeInfo> type_infos;
  std::vector<Function *> funcs;
};
