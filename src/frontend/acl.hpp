#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <unordered_map>
#include <vector>

#include "acl-forward.hpp"
#include "constant.hpp"
#include "diagnostics.hpp"
#include "operators.hpp"
#include "token.hpp"
#include "../util/util.hpp"

class Type;

struct Symbol : public variant<Symbol, Function *, Parameter *, Variable *, Struct *, StructField *, Variant *, Enum *, EnumValue *, Property *> {
  enum class Tag { Function, Parameter, Variable, Struct, StructField, Variant, Enum, EnumValue, Property };
  using variant::variant;

  bool isUser() const;
  SourceLocation getDeclLoc() const;
};

struct Bindings {
  std::unordered_map<std::string, std::vector<Symbol>> bindings;

  void bind(std::string_view name, Symbol sym);
  const std::vector<Symbol> *lookup(std::string_view name) const;
};

struct Scope {
  Scope *parent;
  Bindings bindings;

  explicit Scope(Scope *parent) : parent(parent) { }
  void bind(std::string_view name, Symbol sym);
  const std::vector<Symbol> *lookupScope(std::string_view name) const;
  const std::vector<Symbol> *lookup(std::string_view name) const;
};

struct TypeInfo {
  SourceUnit *unit;
  Bindings members;
  std::vector<Function *> make_funcs;
  Function *take = nullptr;
  Function *drop = nullptr;
  std::vector<Property> properties;
  std::vector<std::unique_ptr<Function>> synthesized;

  TypeInfo(SourceUnit *unit) : unit(unit) { }
  TypeInfo(SourceUnit *unit, Bindings members) : unit(unit), members(members) { }
};

struct Program {
  std::vector<std::unique_ptr<SourceUnit>> units;
  // Anything not living inside the scope of a source unit is bound here
  Bindings bindings;
  std::vector<std::unique_ptr<Function>> intrinsics;
};

struct Import {
  std::string path;
  SourceRange range;
  // The source unit being imported, set by Sema.
  SourceUnit *unit = nullptr;
  bool used = false;

  Import(std::string path, SourceRange range) : path(std::move(path)), range(range) { }
};

struct SourceItem : public variant<SourceItem,
    std::unique_ptr<Function>,
    std::unique_ptr<Variable>,
    std::unique_ptr<Struct>,
    std::unique_ptr<Variant>,
    std::unique_ptr<Enum>,
    std::unique_ptr<TypeAlias>> {
  enum class Tag { Function, Variable, Struct, Variant, Enum, TypeAlias };
  using variant::variant;
};

struct SourceUnit {
  std::string path;
  std::vector<Import> imports;
  std::vector<SourceItem> items;
  std::unique_ptr<Scope> scope;

  SourceUnit(std::string path, std::vector<Import> imports, std::vector<SourceItem> items, std::unique_ptr<Scope> scope)
      : path(std::move(path)), imports(std::move(imports)), items(std::move(items)), scope(std::move(scope)) { }
};

struct Qualifier {
  std::unique_ptr<TypeName> type_name;
  const Type *type;

  explicit Qualifier(std::unique_ptr<TypeName> type_name) : type_name(std::move(type_name)), type(nullptr) { }
  explicit Qualifier(const Type *type) : type_name(nullptr), type(type) { }
};

struct Function {
  enum class BuiltinKind {
    SelfMake,    // intrinsic: function T.make(T) returns (T)
    DynArrMake,  // intrinsic
    Make,        // synthesized

    DynArrTake,  // intrinsic
    Take,        // synthesized

    Drop,        // synthesized

    // DynArrReserve,
    DynArrPushBack,
    // DynArrPopBack,

    Assert,
    AssertMsg,
    Panic,
    PanicMsg
  };

  enum class BuiltinLowering {
    Synthesized,
    Intrinsic
  };

  std::optional<BuiltinKind> builtin_kind;
  std::optional<Qualifier> qualifier;
  std::string name;
  std::optional<SourceLocation> loc;
  std::unique_ptr<Parameter> receiver;
  std::vector<Parameter> params;
  std::vector<Parameter> ret_params;
  std::unique_ptr<BlockStmt> body;
  std::unique_ptr<Scope> scope;
  bool is_extern;
  bool is_no_return;
  // Annotations
  bool is_overloaded = false;
  bool is_main = false;

  // For user functions
  Function(std::optional<Qualifier> qualifier, std::string name, SourceLocation loc,
           std::unique_ptr<Parameter> receiver, std::unique_ptr<Scope> scope, bool is_extern, bool is_no_return)
      : qualifier(std::move(qualifier)), name(std::move(name)), loc(loc), receiver(std::move(receiver)),
        body(nullptr), scope(std::move(scope)), is_extern(is_extern), is_no_return(is_no_return) { }

  // For compiler declared functions
  Function(BuiltinKind builtin_kind, std::optional<Qualifier> qualifier, std::string name, std::unique_ptr<Parameter> receiver,
           std::vector<Parameter> params, std::vector<Parameter> ret_params, bool is_extern, bool is_no_return)
      : builtin_kind(builtin_kind), qualifier(std::move(qualifier)), name(std::move(name)), loc(std::nullopt), receiver(std::move(receiver)),
        params(std::move(params)), ret_params(std::move(ret_params)), body(nullptr), scope(nullptr), is_extern(is_extern),
        is_no_return(is_no_return) { }

  const Type *attachedTo() const;
  bool isStaticMethod() const { return this->qualifier.has_value() && this->receiver == nullptr; }
  bool isSelfMake() const { return this->builtin_kind.has_value() && this->builtin_kind.value() == BuiltinKind::SelfMake; }

  bool hasMainSignature() const;
  // These must be called on a member function of the given type
  bool hasMakeSignature(const Type *type) const;
  bool hasTakeSignature(const Type *type) const;
  bool hasDropSignature(const Type *type) const;

  bool hasSameOverloadSignature(const Function *other) const;
  std::vector<const Type *> flattenedParamTypes() const;
  std::vector<const Parameter *> getCallParams(bool is_bound) const;
  std::string signatureString() const;

  BuiltinLowering builtinLowering() const;
};

struct Parameter {
  std::optional<std::string> name;
  std::optional<SourceLocation> loc;
  std::unique_ptr<TypeName> type_name;
  const Type *type;
  bool is_return;

  Parameter(std::optional<std::string> name, std::optional<SourceLocation> loc, std::unique_ptr<TypeName> type_name, bool is_return)
      : name(std::move(name)), loc(loc), type_name(std::move(type_name)), type(nullptr), is_return(is_return) { }
  Parameter(std::optional<std::string> name, const Type *type, bool is_return)
      : name(std::move(name)), type_name(nullptr), type(type), is_return(is_return) { }

  bool isNamed() const { return this->name.has_value(); }
  const std::string &getName() const { return this->name.value(); }
};

struct Variable {
  std::string name;
  SourceLocation loc;
  std::unique_ptr<TypeName> type_name;
  std::unique_ptr<Expr> init;  // May be null
  bool is_constant;
  // Annotations
  const Type *type = nullptr;
  Constant *constant = nullptr;

  Variable(std::string name, SourceLocation loc, std::unique_ptr<TypeName> type_name, std::unique_ptr<Expr> init, bool is_constant)
      : name(std::move(name)), loc(loc), type_name(std::move(type_name)), init(std::move(init)), is_constant(is_constant) { }
};

struct Struct {
  std::string name;
  SourceLocation loc;
  std::vector<StructField> fields;
  std::unique_ptr<Scope> scope;

  Struct(std::string name, SourceLocation loc, std::unique_ptr<Scope> scope)
      : name(std::move(name)), loc(loc), scope(std::move(scope)) { }
};

struct StructField {
  std::string name;
  SourceLocation loc;
  std::unique_ptr<TypeName> type_name;
  const Type *type = nullptr;

  StructField(std::string name, SourceLocation loc, std::unique_ptr<TypeName> type_name)
      : name(std::move(name)), loc(loc), type_name(std::move(type_name)) { }
};

struct Variant {
  std::string name;
  SourceLocation loc;
  std::vector<VariantAlternative> alternatives;
  std::unique_ptr<Scope> scope;

  Variant(std::string name, SourceLocation loc, std::unique_ptr<Scope> scope)
      : name(std::move(name)), loc(loc), scope(std::move(scope)) { }
};

struct VariantAlternative {
  std::unique_ptr<TypeName> type_name;
  const Type *type = nullptr;

  VariantAlternative(std::unique_ptr<TypeName> type_name) : type_name(std::move(type_name)) { }
};

struct Enum {
  std::string name;
  SourceLocation loc;
  std::vector<EnumValue> values;
  std::unique_ptr<Scope> scope;

  Enum(std::string name, SourceLocation loc, std::unique_ptr<Scope> scope)
      : name(std::move(name)), loc(loc), scope(std::move(scope)) { }
};

struct EnumValue {
  std::string name;
  SourceLocation loc;

  EnumValue(std::string name, SourceLocation loc) : name(std::move(name)), loc(loc) { }
};

struct TypeAlias {
  std::string name;
  SourceLocation loc;
  std::unique_ptr<TypeName> type_name;
  const Type *type = nullptr;

  TypeAlias(std::string name, SourceLocation loc, std::unique_ptr<TypeName> type_name)
      : name(std::move(name)), loc(loc), type_name(std::move(type_name)) { }
};

struct Property {
  std::string name;
  const Type *type;

  Property(std::string name, const Type *type) : name(std::move(name)), type(type) { }
};

// Type names

struct TypeName {
  enum class Tag {
    #define X(TypeName_) TypeName_,
    #include "../defs/type-name-tags.def"
    #undef X
  };

  Tag tag;
  SourceRange range;

  TypeName(Tag tag, SourceRange range) : tag(tag), range(range) { }
  virtual ~TypeName() = default;

  #define X(TypeName_)                      \
    bool is##TypeName_() const;             \
    TypeName_ *to##TypeName_();             \
    const TypeName_ *to##TypeName_() const;
  #include "../defs/type-name-tags.def"
  #undef X
};

struct ElementaryTypeName : public TypeName {
  Keyword keyword;
  ElementaryTypeName(SourceRange range, Keyword keyword)
      : TypeName(Tag::ElementaryTypeName, range), keyword(keyword) { }
};

struct PointerTypeName : public TypeName {
  std::unique_ptr<TypeName> pointee;
  bool is_owning;
  PointerTypeName(SourceRange range, std::unique_ptr<TypeName> pointee, bool is_owning)
      : TypeName(Tag::PointerTypeName, range), pointee(std::move(pointee)), is_owning(is_owning) { }
};

struct ArrayTypeName : public TypeName {
  std::unique_ptr<TypeName> element;
  std::unique_ptr<Expr> length;  // May be null
  ArrayTypeName(SourceRange range, std::unique_ptr<TypeName> element, std::unique_ptr<Expr> length)
      : TypeName(Tag::ArrayTypeName, range), element(std::move(element)), length(std::move(length)) { }
};

struct SliceTypeName : public TypeName {
  std::unique_ptr<TypeName> element;
  SliceTypeName(SourceRange range, std::unique_ptr<TypeName> element)
      : TypeName(Tag::SliceTypeName, range), element(std::move(element)) { }
};

struct IdentifierTypeName : public TypeName {
  std::string name;
  IdentifierTypeName(SourceRange range, std::string name)
      : TypeName(Tag::IdentifierTypeName, range), name(std::move(name)) { }
};

struct ParenthesizedTypeName : public TypeName {
  std::unique_ptr<TypeName> type_name;
  ParenthesizedTypeName(SourceRange range, std::unique_ptr<TypeName> type_name)
      : TypeName(Tag::ParenthesizedTypeName, range), type_name(std::move(type_name)) { }
};

// Statements

struct Stmt {
  enum class Tag {
    #define X(TypeName) TypeName,
    #include "../defs/stmt-tags.def"
    #undef X
  };

  Tag tag;
  SourceRange range;

  Stmt(Tag tag, SourceRange range) : tag(tag), range(range) { }
  virtual ~Stmt() = default;

  #define X(TypeName)                        \
    bool is##TypeName() const;               \
    TypeName *to##TypeName();                \
    const TypeName *to##TypeName() const;
  #include "../defs/stmt-tags.def"
  #undef X
};

struct BlockStmt : public Stmt {
  std::vector<std::unique_ptr<Stmt>> stmts;
  std::unique_ptr<Scope> scope;

  BlockStmt(SourceRange range, std::vector<std::unique_ptr<Stmt>> stmts, std::unique_ptr<Scope> scope)
      : Stmt(Tag::BlockStmt, range), stmts(std::move(stmts)), scope(std::move(scope)) { }
};

struct IfStmt : public Stmt {
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Stmt> then;
  std::unique_ptr<Stmt> else_;  // May be null

  IfStmt(SourceRange range, std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> then, std::unique_ptr<Stmt> else_)
      : Stmt(Tag::IfStmt, range), cond(std::move(cond)), then(std::move(then)), else_(std::move(else_)) { }
};

struct ForStmt : public Stmt {
  std::unique_ptr<Stmt> init;  // May be null. If present, it is a VarDeclStmt or an ExprStmt.
  std::unique_ptr<Expr> cond;  // May be null
  std::unique_ptr<Expr> step;  // May be null
  std::unique_ptr<Stmt> body;
  std::unique_ptr<Scope> scope;

  ForStmt(SourceRange range, std::unique_ptr<Stmt> init, std::unique_ptr<Expr> cond,
          std::unique_ptr<Expr> step, std::unique_ptr<Stmt> body, std::unique_ptr<Scope> scope)
      : Stmt(Tag::ForStmt, range), init(std::move(init)), cond(std::move(cond)), step(std::move(step)), body(std::move(body)), scope(std::move(scope)) { }
};

struct WhileStmt : public Stmt {
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Stmt> body;

  WhileStmt(SourceRange range, std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> body)
      : Stmt(Tag::WhileStmt, range), cond(std::move(cond)), body(std::move(body)) { }
};

struct LabeledStmt : public Stmt {
  std::string label;
  std::unique_ptr<Stmt> stmt;

  LabeledStmt(SourceRange range, std::string label, std::unique_ptr<Stmt> stmt)
      : Stmt(Tag::LabeledStmt, range), label(std::move(label)), stmt(std::move(stmt)) { }
};

struct SwitchStmt : public Stmt {
  std::unique_ptr<Expr> cond;  // May be null
  std::vector<SwitchClause> clauses;

  SwitchStmt(SourceRange range, std::unique_ptr<Expr> cond, std::vector<SwitchClause> clauses)
      : Stmt(Tag::SwitchStmt, range), cond(std::move(cond)), clauses(std::move(clauses)) { }
};

struct SwitchClause {
  std::unique_ptr<Expr> label;  // If null, this is the default case
  std::vector<std::unique_ptr<Stmt>> body;
  SourceLocation label_loc;  // Location of "case" or "default" keyword
  VariantAlternative *alternative = nullptr;

  SwitchClause(std::unique_ptr<Expr> label, std::vector<std::unique_ptr<Stmt>> body, SourceLocation label_loc)
      : label(std::move(label)), body(std::move(body)), label_loc(label_loc) { }

  bool isDefault() const { return this->label == nullptr; }
  bool missingBody() const { return this->body.empty(); }
};

struct FallthroughStmt : public Stmt {
  FallthroughStmt(SourceRange range) : Stmt(Tag::FallthroughStmt, range) { }
};

struct ExprStmt : public Stmt {
  std::unique_ptr<Expr> expr;
  ExprStmt(SourceRange range, std::unique_ptr<Expr> expr) : Stmt(Tag::ExprStmt, range), expr(std::move(expr)) { }
};

struct SkipStmt : public Stmt {
  SkipStmt(SourceRange range) : Stmt(Tag::SkipStmt, range) { }
};

struct BreakStmt : public Stmt {
  std::optional<std::string> label;
  BreakStmt(SourceRange range, std::optional<std::string> label)
      : Stmt(Tag::BreakStmt, range), label(std::move(label)) { }
};

struct ContinueStmt : public Stmt {
  std::optional<std::string> label;
  ContinueStmt(SourceRange range, std::optional<std::string> label)
      : Stmt(Tag::ContinueStmt, range), label(std::move(label)) { }
};

struct VarDeclStmt : public Stmt {
  std::vector<std::unique_ptr<Variable>> vars;
  std::unique_ptr<Expr> init;  // May be null

  VarDeclStmt(SourceRange range, std::vector<std::unique_ptr<Variable>> vars, std::unique_ptr<Expr> init)
      : Stmt(Tag::VarDeclStmt, range), vars(std::move(vars)), init(std::move(init)) { }
};

struct ReturnStmt : public Stmt {
  std::unique_ptr<Expr> expr;  // May be null
  ReturnStmt(SourceRange range, std::unique_ptr<Expr> expr) : Stmt(Tag::ReturnStmt, range), expr(std::move(expr)) { }
};

// Expressions

struct Typed {
  const Type *type;
  explicit Typed(const Type *type) : type(type) { }
};

struct UntypedIntegerConstant { };
struct UntypedStringConstant { };
struct UntypedNullConstant { };
struct UntypedCompositeLiteral { };

struct TypeState : public variant<TypeState, Typed, UntypedIntegerConstant, UntypedStringConstant, UntypedNullConstant, UntypedCompositeLiteral> {
 public:
  enum class Tag { Typed, UntypedIntegerConstant, UntypedStringConstant, UntypedNullConstant, UntypedCompositeLiteral };
  using variant::variant;

  std::string_view untypedDesc() const;
};

struct ExprAnno {
  ExprAnno(TypeState type_state, const Constant *constant, bool is_lvalue)
      : type_state(type_state), constant(constant), is_lvalue(is_lvalue) { }

  TypeState type_state;
  const Constant *constant;
  bool is_lvalue;
};

struct Expr {
  enum class Tag {
    #define X(TypeName) TypeName,
    #include "../defs/expr-tags.def"
    #undef X
  };

  Tag tag;
  SourceRange range;
  std::optional<ExprAnno> anno;

  Expr(Tag tag, SourceRange range) : tag(tag), range(range) { }
  virtual ~Expr() = default;

  #define X(TypeName)                        \
    bool is##TypeName() const;               \
    TypeName *to##TypeName();                \
    const TypeName *to##TypeName() const;
  #include "../defs/expr-tags.def"
  #undef X

  bool isAnnotated() const { return this->anno.has_value(); }
  // These helpers assume the expression is annotated
  bool isTyped() const { return this->anno.value().type_state.is<Typed>(); }
  bool isUntyped() const { return !this->isTyped(); }
  const Type *getType() const { return this->anno.value().type_state.get<Typed>().type; }
  bool isConstant() const { return this->anno.value().constant != nullptr; };
  const Constant *getConstant() const { return this->anno.value().constant; }

  Expr *unparenthesize();
  const Expr *unparenthesize() const;

  std::vector<Expr *> extractComponents();
  std::vector<const Expr *> extractComponents() const;

  bool referencesVariable(const Variable *var) const;
};

struct AssignmentExpr : public Expr {
  AssignmentOp op;
  std::unique_ptr<Expr> left;
  std::unique_ptr<Expr> right;
  SourceRange op_range;

  AssignmentExpr(SourceRange range, AssignmentOp op, std::unique_ptr<Expr> left, std::unique_ptr<Expr> right, SourceRange op_range)
      : Expr(Tag::AssignmentExpr, range), op(op), left(std::move(left)), right(std::move(right)), op_range(op_range) { }
};

struct UnaryExpr : public Expr {
  UnaryOp op;
  std::unique_ptr<Expr> operand;
  SourceRange op_range;

  UnaryExpr(SourceRange range, UnaryOp op, std::unique_ptr<Expr> operand, SourceRange op_range)
      : Expr(Tag::UnaryExpr, range), op(op), operand(std::move(operand)), op_range(op_range) { }
};

struct BinaryExpr : public Expr {
  BinaryOp op;
  std::unique_ptr<Expr> left;
  std::unique_ptr<Expr> right;
  SourceRange op_range;

  BinaryExpr(SourceRange range, BinaryOp op, std::unique_ptr<Expr> left, std::unique_ptr<Expr> right, SourceRange op_range)
      : Expr(Tag::BinaryExpr, range), op(op), left(std::move(left)), right(std::move(right)), op_range(op_range) { }
};

struct FunctionCall {
  Function *func;
  bool is_bound_method_call;
  FunctionCall(Function *func, bool is_bound_method_call) : func(func), is_bound_method_call(is_bound_method_call) { }
};

// T(args) or new T(args) where T is a type name
struct TypeCall {
  const Type *type;
  Function *make_func;  // may be null
  TypeCall(const Type *type, Function *make_func) : type(type), make_func(make_func) { }
};

struct CallKind : public variant<CallKind, FunctionCall, TypeCall> {
  enum class Tag { FunctionCall, TypeCall };
  using variant::variant;
};

struct CallArg {
  struct Name {
    std::string text;
    SourceRange range;
    Name(std::string text, SourceRange range) : text(std::move(text)), range(range) { }
  };

  std::unique_ptr<Expr> value;
  std::optional<Name> name;

  CallArg(std::unique_ptr<Expr> value, std::optional<Name> name)
      : value(std::move(value)), name(std::move(name)) { }

  bool isNamed() const { return name.has_value(); }
};

struct CallExpr : public Expr {
  std::unique_ptr<Expr> callee;
  std::vector<CallArg> arguments;
  SourceLocation lparen_loc;
  std::optional<CallKind> call_kind;

  CallExpr(SourceRange range, std::unique_ptr<Expr> callee, std::vector<CallArg> arguments, SourceLocation lparen_loc)
      : Expr(Tag::CallExpr, range), callee(std::move(callee)), arguments(std::move(arguments)), lparen_loc(lparen_loc) { }

  bool isNew() const { return this->callee->isNewExpr(); }
};

struct IndexAccessExpr : public Expr {
  std::unique_ptr<Expr> base;
  std::unique_ptr<Expr> index;
  SourceLocation lsquare_loc;

  IndexAccessExpr(SourceRange range, std::unique_ptr<Expr> base, std::unique_ptr<Expr> index, SourceLocation lsquare_loc)
      : Expr(Tag::IndexAccessExpr, range), base(std::move(base)), index(std::move(index)), lsquare_loc(lsquare_loc) { }
};

struct MemberAccessExpr : public Expr {
  std::unique_ptr<Expr> base;
  std::string member_name;
  bool is_arrow;
  SourceLocation dot_loc;  // Location of '.' (or '->')
  SourceRange member_range;
  // Annotations
  StructField *field = nullptr;
  Property *property = nullptr;

  MemberAccessExpr(SourceRange range, std::unique_ptr<Expr> base, std::string member_name, bool is_arrow, SourceLocation dot_loc, SourceRange member_range)
      : Expr(Tag::MemberAccessExpr, range), base(std::move(base)), member_name(std::move(member_name)), is_arrow(is_arrow), dot_loc(dot_loc), member_range(member_range) { }
};

struct VariantAccessExpr : public Expr {
  std::unique_ptr<Expr> base;
  std::unique_ptr<TypeName> type_name;
  bool is_arrow;
  SourceLocation dot_loc;  // Location of '.' (or '->')
  // Annotation
  VariantAlternative *alternative = nullptr;

  VariantAccessExpr(SourceRange range, std::unique_ptr<Expr> base, std::unique_ptr<TypeName> type_name, bool is_arrow, SourceLocation dot_loc)
      : Expr(Tag::VariantAccessExpr, range), base(std::move(base)), type_name(std::move(type_name)), is_arrow(is_arrow), dot_loc(dot_loc) { }
};

struct TypeTestExpr : public Expr {
  std::unique_ptr<Expr> operand;
  std::unique_ptr<TypeName> type_name;
  bool is_negated;
  // Annotation
  VariantAlternative *alternative = nullptr;

  TypeTestExpr(SourceRange range, std::unique_ptr<Expr> operand, std::unique_ptr<TypeName> type_name, bool is_negated)
      : Expr(Tag::TypeTestExpr, range), operand(std::move(operand)), type_name(std::move(type_name)), is_negated(is_negated) { }
};

struct TypeCastExpr : public Expr {
  std::unique_ptr<Expr> operand;
  std::unique_ptr<TypeName> type_name;
  bool is_exclaimed;
  // Annotation
  const Type *target = nullptr;

  TypeCastExpr(SourceRange range, std::unique_ptr<Expr> operand, std::unique_ptr<TypeName> type_name, bool is_exclaimed)
      : Expr(Tag::TypeCastExpr, range), operand(std::move(operand)), type_name(std::move(type_name)), is_exclaimed(is_exclaimed) { }
};

struct IdentifierExpr : public Expr {
  std::string name;
  // Annotations
  Variable *var = nullptr;
  Parameter *param = nullptr;

  IdentifierExpr(SourceRange range, std::string name)
      : Expr(Tag::IdentifierExpr, range), name(std::move(name)) { }
};

struct ParenExpr : public Expr {
  std::unique_ptr<Expr> expr;
  ParenExpr(SourceRange range, std::unique_ptr<Expr> expr) : Expr(Tag::ParenExpr, range), expr(std::move(expr)) { }
};

struct ListExpr : public Expr {
  std::vector<std::unique_ptr<Expr>> components;
  ListExpr(SourceRange range, std::vector<std::unique_ptr<Expr>> components)
      : Expr(Tag::ListExpr, range), components(std::move(components)) { }
};

struct CompositeLiteralExpr : public Expr {
  std::vector<std::unique_ptr<Expr>> components;
  CompositeLiteralExpr(SourceRange range, std::vector<std::unique_ptr<Expr>> components)
      : Expr(Tag::CompositeLiteralExpr, range), components(std::move(components)) { }
};

struct NewExpr : public Expr {
  std::unique_ptr<TypeName> type_name;
  NewExpr(SourceRange range, std::unique_ptr<TypeName> type_name)
      : Expr(Tag::NewExpr, range), type_name(std::move(type_name)) { }
};

// A type name used in the context of an expression. Includes elementary, array, slice,
// and owning pointer type names. Pointer type names are parsed as unary expressions, parenthesized
// type names as parenthesized expressions and identifier type names as identifier expressions.

struct TypeNameExpr : public Expr {
  std::unique_ptr<TypeName> type_name;
  TypeNameExpr(SourceRange range, std::unique_ptr<TypeName> type_name)
      : Expr(Tag::TypeNameExpr, range), type_name(std::move(type_name)) { }
};

struct IntLiteralExpr : public Expr {
  mpz_class value;
  IntLiteralExpr(SourceRange range, mpz_class value) : Expr(Tag::IntLiteralExpr, range), value(std::move(value)) { }
};

struct BoolLiteralExpr : public Expr {
  bool value;
  BoolLiteralExpr(SourceRange range, bool value) : Expr(Tag::BoolLiteralExpr, range), value(value) { }
};

struct NullExpr : public Expr {
  NullExpr(SourceRange range) : Expr(Tag::NullExpr, range) { }
};

struct StringLiteralExpr : public Expr {
  std::string value;
  StringLiteralExpr(SourceRange range, std::string value) : Expr(Tag::StringLiteralExpr, range), value(std::move(value)) { }
};

struct CharLiteralExpr : public Expr {
  char value;
  CharLiteralExpr(SourceRange range, char value) : Expr(Tag::CharLiteralExpr, range), value(value) { }
};
