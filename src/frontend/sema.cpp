#include "sema.hpp"

#include <memory>
#include <unordered_set>

#include "constant.hpp"
#include "constant-provider.hpp"
#include "type.hpp"
#include "type-provider.hpp"
#include "../util/util.hpp"

SemaResult Sema::process() {
  this->initializeTypeInfos();
  this->registerIntrinsics();

  for (auto &unit : this->program->units)
    this->registerSymbols(unit.get());

  for (auto &unit : this->program->units)
    this->checkImports(unit.get());

  for (auto &unit : this->program->units)
    this->resolveTypeNames(unit.get());

  for (auto &unit : this->program->units)
    this->checkTypes(unit.get());

  this->completeTypeInfos();
  this->checkOverloads();

  for (auto &unit : this->program->units)
    this->checkFunctionBodies(unit.get());

  this->checkMain();
  this->checkUnusedImports();
  return SemaResult(std::move(this->type_infos), std::move(this->funcs));
}

void Sema::initializeTypeInfos() {
  this->type_infos.insert({TypeProvider::Int8(),   TypeInfo(nullptr)});
  this->type_infos.insert({TypeProvider::Int16(),  TypeInfo(nullptr)});
  this->type_infos.insert({TypeProvider::Int32(),  TypeInfo(nullptr)});
  this->type_infos.insert({TypeProvider::Int64(),  TypeInfo(nullptr)});
  this->type_infos.insert({TypeProvider::UInt8(),  TypeInfo(nullptr)});
  this->type_infos.insert({TypeProvider::UInt16(), TypeInfo(nullptr)});
  this->type_infos.insert({TypeProvider::UInt32(), TypeInfo(nullptr)});
  this->type_infos.insert({TypeProvider::UInt64(), TypeInfo(nullptr)});
  this->type_infos.insert({TypeProvider::Bool(),   TypeInfo(nullptr)});
  this->type_infos.insert({TypeProvider::Char(),   TypeInfo(nullptr)});
  SourceUnit *string_unit = this->program->units.at(1).get();
  this->type_infos.insert({TypeProvider::String(), TypeInfo(string_unit)});
  this->type_infos.insert({TypeProvider::Str(),    TypeInfo(nullptr)});
}

void Sema::registerSymbols(SourceUnit *unit) {
  for (const SourceItem &item : unit->items) {
    switch (item.getTag()) {
    case SourceItem::Tag::Function: {
      Function *func = item.get<std::unique_ptr<Function>>().get();
      this->funcs.push_back(func);

      if (func->receiver == nullptr && !func->qualifier.has_value())
        this->bindSymbol(func->name, Symbol(func), unit->scope.get(), unit);

      if (func->receiver != nullptr && func->receiver->isNamed())
        this->bindSymbol(func->receiver->getName(), Symbol(func->receiver.get()), func->scope.get(), unit);

      for (Parameter &param : func->params) {
        if (param.isNamed())
          this->bindSymbol(param.getName(), Symbol(&param), func->scope.get(), unit);
      }

      continue;
    }
    case SourceItem::Tag::Variable: {
      assert(false && "unimplemented");
    }
    case SourceItem::Tag::Struct: {
      Struct *struct_ = item.get<std::unique_ptr<Struct>>().get();

      this->bindSymbol(struct_->name, Symbol(struct_), unit->scope.get(), unit);
      for (StructField &field : struct_->fields)
        this->bindSymbol(field.name, Symbol(&field), struct_->scope.get(), unit);

      // Initialize type info with everything the lexical scope has
      this->type_infos.insert({TypeProvider::Struct(struct_), TypeInfo(unit, struct_->scope->bindings)});
      continue;
    }
    case SourceItem::Tag::Variant: {
      Variant *variant = item.get<std::unique_ptr<Variant>>().get();

      this->bindSymbol(variant->name, Symbol(variant), unit->scope.get(), unit);
      this->type_infos.insert({TypeProvider::Variant(variant), TypeInfo(unit, variant->scope->bindings)});
      continue;
    }
    case SourceItem::Tag::Enum: {
      Enum *enum_ = item.get<std::unique_ptr<Enum>>().get();

      this->bindSymbol(enum_->name, Symbol(enum_), unit->scope.get(), unit);
      for (EnumValue &value : enum_->values)
        this->bindSymbol(value.name, Symbol(&value), enum_->scope.get(), unit);

      this->type_infos.insert({TypeProvider::Enum(enum_), TypeInfo(unit, enum_->scope->bindings)});
      continue;
    }
    case SourceItem::Tag::TypeAlias: {
      assert(false && "unimplemented");
    }
    }

    assert(false);
  }
}

void Sema::bindSymbol(std::string_view name, Symbol sym, Scope *scope, SourceUnit *unit) {
  if (!this->bindSymbol(name, sym, &scope->bindings))
    return;

  // Check for shadowded symbols

  if (sym.getTag() == Symbol::Tag::StructField
      || sym.getTag() == Symbol::Tag::EnumValue
      || sym.getTag() == Symbol::Tag::Property)
    return;

  if (scope->parent != nullptr) {
    const std::vector<Symbol> *shadowed = this->lookupSymbol(name, scope->parent, unit);
    if (shadowed != nullptr)
      return this->warnShadowing(name, sym, shadowed);
  }

  // Check program scope
  const std::vector<Symbol> *shadowed = this->program->bindings.lookup(name);
  if (shadowed != nullptr)
    return this->warnShadowing(name, sym, shadowed);
}

void Sema::warnShadowing(std::string_view name, Symbol sym, const std::vector<Symbol> *shadowed) {
  if (shadowed->front().isUser()) {
    this->diag->warning(sym.getDeclLoc(), "'{}' shadows a previous declaration", name);
    this->diag->note(shadowed->front().getDeclLoc(), "shadowed symbol is here");
  } else {
    this->diag->warning(sym.getDeclLoc(), "'{}' shadows a compiler provided symbol", name);
  }
}

bool Sema::bindSymbol(std::string_view name, Symbol sym, Bindings *bindings) {
  const std::vector<Symbol> *declared = bindings->lookup(name);

  if (declared == nullptr || (declared->front().getTag() == Symbol::Tag::Function && sym.getTag() == Symbol::Tag::Function)) {
    bindings->bind(name, sym);
    return true;
  } else {
    assert(!declared->empty());

    if (sym.isUser()) {
      // When binding a user symbol, it can only conflict with another user symbol,
      // because the compiler registers builtins after all source has been processed.
      this->diag->error(sym.getDeclLoc(),
                        "symbol '{}' already bound in scope", name);
      this->diag->note(declared->back().getDeclLoc(), "here");
    } else {
      // Compiler tried to bind a symbol and had conflict -> blame the user symbol
      this->diag->error(declared->front().getDeclLoc(),
                        "'{}' conflicts with a compiler-provided symbol in this scope", name);
    }

    return false;
  }
}

const std::vector<Symbol> *Sema::lookupSymbol(std::string_view name, const Scope *scope, SourceUnit *unit) {
  assert(unit != nullptr);

  const std::vector<Symbol> *syms = scope->lookup(name);
  if (syms != nullptr)
    return syms;

  for (Import &import : unit->imports) {
    const std::vector<Symbol> *syms = import.unit->scope->lookupScope(name);
    if (syms != nullptr) {
      import.used = true;
      return syms;
    }
  }

  return this->program->bindings.lookup(name);
}

void Sema::checkImports(SourceUnit *unit) {
  std::unordered_set<SourceUnit *> seen;

  // For each name, store which import imported it, and the symbols it imported
  std::unordered_map<std::string, ImportInfo> import_info;

  for (const Import &import : unit->imports) {
    if (import.unit == unit) {
      this->diag->error(import.range, "source unit cannot import itself");
      continue;
    }

    if (seen.count(import.unit)) {
      this->diag->error(import.range, "source unit imported twice");
      continue;
    }

    seen.insert(import.unit);

    for (const auto &[name, syms] : import.unit->scope->bindings.bindings) {
      if (import_info.count(name) > 0) {
        const ImportInfo &conflicting = import_info.at(name);

        this->diag->error(import.range, "import conflicts with a previous import; both source units define '{}'", name);
        this->diag->note(conflicting.import->range, "conflicting import is here");
        this->diag->note(syms.front().getDeclLoc(), "imported symbol is here");
        this->diag->note(conflicting.syms->front().getDeclLoc(), "conflicting symbol is here");
        continue;
      }

      import_info.insert({name, ImportInfo(&import, &syms)});
    }
  }

  // Check if the source unit defines something already imported
  for (const auto &[name, syms] : unit->scope->bindings.bindings) {
    if (import_info.count(name) > 0) {
      const ImportInfo &conflicting = import_info.at(name);

      this->diag->error(syms.front().getDeclLoc(), "'{}' conflicts with an imported symbol", name);
      this->diag->note(conflicting.import->range, "conflicting import is here");
      this->diag->note(conflicting.syms->front().getDeclLoc(), "conflicting symbol is here");
    }
  }
}

void Sema::resolveTypeNames(SourceUnit *unit) {
  for (SourceItem &item : unit->items) {
    switch (item.getTag()) {
    case SourceItem::Tag::Function: {
      Function *func = item.get<std::unique_ptr<Function>>().get();
      TypeNameContext type_ctx(unit, func->scope.get());

      if (func->qualifier.has_value()) {
        const Type *type = this->resolve(func->qualifier.value().type_name.get(), type_ctx);

        if (type != nullptr) {
          if (type->isPointerType()) {
            this->diag->error(func->qualifier.value().type_name->range,
                              "illegal qualifier type '{}'", type->toString());
          } else {
            func->qualifier.value().type = type;

            TypeInfo *info = &this->type_infos.at(type);

            if (info->unit == nullptr) {
              this->diag->error(func->loc.value(),
                                "cannot define method on '{}'",
                                type->toString());
            } else if (info->unit != unit) {
              this->diag->error(func->loc.value(),
                                "cannot define method on '{}' outside its defining source unit",
                                type->toString());
            } else {
              this->bindSymbol(func->name, Symbol(func), &info->members);
            }
          }
        }
      }

      if (func->receiver != nullptr) {
        const Type *receiver_type = this->resolve(func->receiver->type_name.get(), type_ctx);

        if (receiver_type != nullptr) {
          func->receiver->type = receiver_type;

          const Type *receiver_base = receiver_type->isPointerType() ? receiver_type->toPointerType()->getPointee() : receiver_type;

          if (receiver_base->isPointerType()) {
            this->diag->error(func->receiver->type_name->range,
                              "illegal receiver base type '{}'", receiver_base->toString());
          } else if (func->qualifier.has_value()) {
            const Type *qual_type = func->qualifier.value().type;

            if (qual_type != nullptr) {
              if (!receiver_base->isEqualTo(qual_type)) {
                this->diag->error(func->receiver->type_name->range,
                                 "receiver base type '{}' does not match qualifier type '{}'",
                                 receiver_base->toString(), qual_type->toString());
              } else {
                this->diag->warning(func->qualifier.value().type_name->range,
                                    "qualifier may be omitted");
              }
            }
          } else {
            TypeInfo *info = &this->type_infos.at(receiver_base);

            if (info->unit == nullptr) {
              this->diag->error(func->loc.value(),
                                "cannot define method on '{}'",
                                receiver_base->toString());
            } else if (info->unit != unit) {
              this->diag->error(func->loc.value(),
                                "cannot define method on '{}' outside its defining source unit",
                                receiver_base->toString());
            } else {
              this->bindSymbol(func->name, Symbol(func), &info->members);
            }
          }
        }
      }

      for (Parameter &param : func->params) {
        const Type *type = this->resolve(param.type_name.get(), type_ctx);
        if (type != nullptr)
          param.type = type;
      }

      for (Parameter &ret_param : func->ret_params) {
        const Type *type = this->resolve(ret_param.type_name.get(), type_ctx);
        if (type != nullptr)
          ret_param.type = type;
      }

      continue;
    }
    case SourceItem::Tag::Variable: {
      assert(false && "unimplemented");
    }
    case SourceItem::Tag::Struct: {
      Struct *struct_ = item.get<std::unique_ptr<Struct>>().get();
      TypeNameContext type_ctx(unit, struct_->scope.get());

      for (StructField &field : struct_->fields) {
        const Type *type = this->resolve(field.type_name.get(), type_ctx);
        if (type != nullptr)
          field.type = type;
      }

      continue;
    }
    case SourceItem::Tag::Variant: {
      Variant *variant = item.get<std::unique_ptr<Variant>>().get();
      TypeNameContext type_ctx(unit, variant->scope.get());

      for (VariantAlternative &alt : variant->alternatives) {
        const Type *type = this->resolve(alt.type_name.get(), type_ctx);
        if (type != nullptr)
          alt.type = type;
      }

      continue;
    }
    case SourceItem::Tag::Enum: {
      continue;
    }
    case SourceItem::Tag::TypeAlias: {
      assert(false && "unimplemented");
    }
    }

    assert(false);
  }
}

void Sema::checkTypes(SourceUnit *unit) {
  for (const SourceItem &item : unit->items) {
    switch (item.getTag()) {
    case SourceItem::Tag::Function:
    case SourceItem::Tag::Variable:
      continue;
    case SourceItem::Tag::Struct: {
      Struct *struct_ = item.get<std::unique_ptr<Struct>>().get();

      const Type *start = TypeProvider::Struct(struct_);
      this->checkDependencyCycle(start, struct_->loc);

      continue;
    }
    case SourceItem::Tag::Variant: {
      Variant *variant = item.get<std::unique_ptr<Variant>>().get();

      std::unordered_set<const Type *> seen_types;

      for (VariantAlternative &alt : variant->alternatives) {
        if (alt.type == nullptr)
          continue;

        if (seen_types.count(alt.type)) {
          this->diag->error(alt.type_name->range,
                            "duplicate alternative '{}'", alt.type->toString());
        } else {
          seen_types.insert(alt.type);
        }
      }

      const Type *start = TypeProvider::Variant(item.get<std::unique_ptr<Variant>>().get());
      this->checkDependencyCycle(start, variant->loc);

      continue;
    }
    case SourceItem::Tag::Enum:
    case SourceItem::Tag::TypeAlias:
      continue;
    }

    assert(false);
  }
}

void Sema::checkDependencyCycle(const Type *start, SourceLocation decl_loc) {
  std::unordered_map<const Type *, const Type *> parent;  // also the visited set
  std::vector<const Type *> worklist;

  parent.emplace(start, nullptr);
  worklist.push_back(start);

  while (!worklist.empty()) {
    const Type *type = worklist.back();
    worklist.pop_back();

    for (const Type *dep : type->sizeDeps()) {
      if (dep == start) {
        std::vector<const Type *> cycle{dep};
        for (const Type *current = type; current != nullptr; current = parent.at(current))
          cycle.push_back(current);
        std::reverse(cycle.begin(), cycle.end());

        std::string path;
        for (size_t i = 0, n = cycle.size(); i < n; ++i) {
          path += "'" + cycle[i]->toString() + "'";
          if (i < n - 1)
            path += " -> ";
        }

        return this->diag->error(decl_loc, "type '{}' has a dependency cycle ({})", start->toString(), path);
      }

      if (parent.try_emplace(dep, type).second)
        worklist.push_back(dep);
    }
  }
}

const Type *Sema::ensureTypeRegistered(const Type *type) {
  // type is []T or [*]T or [N]T
  if (this->type_infos.find(type) != this->type_infos.end())
    return type;

  this->type_infos.insert({type, TypeInfo(nullptr)});

  if (this->type_infos_populated)
    this->populateTypeInfo(type);

  return type;
}

void Sema::registerIntrinsics() {
  {
    std::vector<Parameter> assert_params;
    assert_params.emplace_back("condition", TypeProvider::Bool(), false);
    std::unique_ptr<Function> assert = std::make_unique<Function>(Function::BuiltinKind::Assert, std::nullopt, "assert",
                                                                  nullptr, std::move(assert_params), std::vector<Parameter>{}, false, false);

    this->funcs.push_back(assert.get());
    this->bindSymbol("assert", Symbol(assert.get()), &this->program->bindings);
    this->program->intrinsics.push_back(std::move(assert));
  }

  {
    std::vector<Parameter> assert_msg_params;
    assert_msg_params.emplace_back("condition", TypeProvider::Bool(), false);
    assert_msg_params.emplace_back("message", TypeProvider::Str(), false);
    std::unique_ptr<Function> assert_msg = std::make_unique<Function>(Function::BuiltinKind::AssertMsg, std::nullopt, "assert",
                                                                      nullptr, std::move(assert_msg_params), std::vector<Parameter>{}, false, false);

    this->funcs.push_back(assert_msg.get());
    this->bindSymbol("assert", Symbol(assert_msg.get()), &this->program->bindings);
    this->program->intrinsics.push_back(std::move(assert_msg));
  }

  {
    std::unique_ptr<Function> panic = std::make_unique<Function>(Function::BuiltinKind::Panic, std::nullopt, "panic", nullptr,
                                                                 std::vector<Parameter>{}, std::vector<Parameter>{}, false, true);

    this->funcs.push_back(panic.get());
    this->bindSymbol("panic", Symbol(panic.get()), &this->program->bindings);
    this->program->intrinsics.push_back(std::move(panic));
  }

  {
    std::vector<Parameter> panic_msg_params;
    panic_msg_params.emplace_back("message", TypeProvider::Str(), false);
    std::unique_ptr<Function> panic_msg = std::make_unique<Function>(Function::BuiltinKind::Panic, std::nullopt, "panic", nullptr,
                                                                     std::move(panic_msg_params), std::vector<Parameter>{}, false, true);

    this->funcs.push_back(panic_msg.get());
    this->bindSymbol("panic", Symbol(panic_msg.get()), &this->program->bindings);
    this->program->intrinsics.push_back(std::move(panic_msg));
  }
}

void Sema::completeTypeInfos() {
  for (auto &[type, info] : this->type_infos)
    this->collectUserTypeInfo(type);

  for (auto &[type, info] : this->type_infos)
    this->populateTypeInfo(type);

  this->type_infos_populated = true;
}

void Sema::collectUserTypeInfo(const Type *type) {
  TypeInfo &info = this->type_infos.at(type);

  const std::vector<Symbol> *make_symbols = info.members.lookup("make");

  if (make_symbols != nullptr) {
    for (const Symbol &symbol : *make_symbols) {
      if (!symbol.is<Function *>() || !symbol.get<Function *>()->hasMakeSignature(type)) {
        this->diag->error(symbol.getDeclLoc(), "invalid signature for 'make'");
      } else {
        info.make_funcs.push_back(symbol.get<Function *>());
      }
    }
  }

  const std::vector<Symbol> *take_symbols = info.members.lookup("take");

  if (take_symbols != nullptr) {
    for (const Symbol &symbol : *take_symbols) {
      if (!symbol.is<Function *>() || !symbol.get<Function *>()->hasTakeSignature(type)) {
        this->diag->error(symbol.getDeclLoc(), "invalid signature for 'take'");
      } else if (info.take != nullptr) {
        this->diag->error(symbol.getDeclLoc(), "redefinition of 'take'");
      } else {
        info.take = symbol.get<Function *>();
      }
    }
  }

  const std::vector<Symbol> *drop_symbols = info.members.lookup("drop");

  if (drop_symbols != nullptr) {
    for (const Symbol &symbol : *drop_symbols) {
      if (!symbol.is<Function *>() || !symbol.get<Function *>()->hasDropSignature(type)) {
        this->diag->error(symbol.getDeclLoc(), "invalid signature for 'drop'");
      } else if (info.drop != nullptr) {
        this->diag->error(symbol.getDeclLoc(), "redefinition of 'drop'");
      } else {
        info.drop = symbol.get<Function *>();
      }
    }
  }
}

void Sema::populateTypeInfo(const Type *type) {
  TypeInfo &info = this->type_infos.at(type);

  if (type->isComposite()) {
    std::unique_ptr<Function> self_make = Sema::getSelfMake(type);
    info.make_funcs.push_back(self_make.get());
    this->bindSymbol("make", Symbol(self_make.get()), &info.members);
    this->funcs.push_back(self_make.get());
    info.synthesized.push_back(std::move(self_make));
  }

  if (type->isStructType()) {
    std::unique_ptr<Function> make = Sema::getStructMake(type->toStructType());
    info.make_funcs.push_back(make.get());
    this->bindSymbol("make", Symbol(make.get()), &info.members);
    this->funcs.push_back(make.get());
    info.synthesized.push_back(std::move(make));
  }

  if (type->isVariantType()) {
    Variant *variant = type->toVariantType()->getVariant();

    for (VariantAlternative &alternative : variant->alternatives) {
      std::unique_ptr<Function> make = Sema::getVariantMake(type->toVariantType(), &alternative);
      info.make_funcs.push_back(make.get());
      this->bindSymbol("make", Symbol(make.get()), &info.members);
      this->funcs.push_back(make.get());
      info.synthesized.push_back(std::move(make));
    }
  }

  // TODO: Get rid of this when we have generics
  if (type->isDynamicArray()) {
    std::unique_ptr<Function> make = getMake(type, {});
    info.make_funcs.push_back(make.get());
    this->bindSymbol("make", Symbol(make.get()), &info.members);
    this->funcs.push_back(make.get());
    info.synthesized.push_back(std::move(make));

    std::unique_ptr<Parameter> receiver = std::make_unique<Parameter>("this", TypeProvider::Pointer(type, false), false);
    std::vector<Parameter> push_back_params;
    push_back_params.emplace_back("t", type->toArrayType()->getElement(), false);
    std::unique_ptr<Function> push_back = std::make_unique<Function>(Function::BuiltinKind::DynArrPushBack, Qualifier(type), "push_back", std::move(receiver),
                                                                     std::move(push_back_params), std::vector<Parameter>(), false, false);
    this->bindSymbol("push_back", Symbol(push_back.get()), &info.members);
    this->funcs.push_back(push_back.get());
    info.synthesized.push_back(std::move(push_back));
  }

  // If the user provided drop, but not take, do not synthesize one.
  if (type->requiresTake(this->type_infos) && info.take == nullptr && info.drop == nullptr) {
    std::unique_ptr<Function> take = getTake(type);
    info.take = take.get();
    this->bindSymbol("take", Symbol(take.get()), &info.members);
    this->funcs.push_back(take.get());
    info.synthesized.push_back(std::move(take));
  }

  if (type->requiresDrop(this->type_infos) && info.drop == nullptr) {
    std::unique_ptr<Function> drop = getDrop(type);
    info.drop = drop.get();
    this->bindSymbol("drop", Symbol(drop.get()), &info.members);
    this->funcs.push_back(drop.get());
    info.synthesized.push_back(std::move(drop));
  }

  if (type->isArrayType() || type->isSliceType() || type->isStringType() || type->isStrType()) {
    Property length("length", TypeProvider::UInt64());
    info.properties.push_back(length);
    this->bindSymbol("length", Symbol(&info.properties.back()), &info.members);
  }
}

std::unique_ptr<Function> Sema::getSelfMake(const Type *type) {
  // function T.make(T) returns (T)
  std::vector<Parameter> params;
  params.emplace_back(std::nullopt, type, false);

  std::vector<Parameter> ret_params;
  ret_params.emplace_back(std::nullopt, type, false);

  return std::make_unique<Function>(Function::BuiltinKind::SelfMake, Qualifier(type), "make", nullptr,
                                    std::move(params), std::move(ret_params), false, false);
}

std::unique_ptr<Function> Sema::getStructMake(const StructType *type) {
  std::vector<Parameter> params;

  for (StructField &field : type->getStruct()->fields) {
    assert(field.type != nullptr);
    params.push_back(Parameter(field.name, field.type, false));
  }

  return getMake(type, std::move(params));
}

std::unique_ptr<Function> Sema::getVariantMake(const VariantType *type, VariantAlternative *alt) {
  assert(alt->type != nullptr);
  std::vector<Parameter> params;
  params.emplace_back(std::nullopt, alt->type, false);

  return getMake(type, std::move(params));
}

std::unique_ptr<Function> Sema::getMake(const Type *type, std::vector<Parameter> params) {
  // function T.make(<params>) returns (T)
  std::vector<Parameter> ret_params;
  ret_params.emplace_back(std::nullopt, type, true);

  Function::BuiltinKind builtin_kind = type->isDynamicArray() ? Function::BuiltinKind::DynArrMake : Function::BuiltinKind::Make;
  return std::make_unique<Function>(builtin_kind, Qualifier(type), "make", nullptr,
                                    std::move(params), std::move(ret_params), type->isStringType(), false);
}

std::unique_ptr<Function> Sema::getTake(const Type *type) {
  // function (t *T) take() returns (T)
  std::unique_ptr<Parameter> receiver = std::make_unique<Parameter>("this", TypeProvider::Pointer(type, false), false);

  std::vector<Parameter> ret_params;
  ret_params.emplace_back(std::nullopt, type, true);

  Function::BuiltinKind builtin_kind = type->isDynamicArray() ? Function::BuiltinKind::DynArrTake : Function::BuiltinKind::Take;
  return std::make_unique<Function>(builtin_kind, Qualifier(type), "take", std::move(receiver), std::vector<Parameter>(), std::move(ret_params), false, false);
}

std::unique_ptr<Function> Sema::getDrop(const Type *type) {
  // function (t *T) drop()
  std::unique_ptr<Parameter> receiver = std::make_unique<Parameter>("this", TypeProvider::Pointer(type, false), false);
  return std::make_unique<Function>(Function::BuiltinKind::Drop, Qualifier(type), "drop", std::move(receiver), std::vector<Parameter>(), std::vector<Parameter>(), false, false);
}

void Sema::checkOverloads() {
  for (const auto &unit : this->program->units)
    this->checkOverloads(&unit->scope->bindings);
  for (auto &[_, info] : this->type_infos)
    this->checkOverloads(&info.members);
}

void Sema::checkOverloads(Bindings *bindings) {
  for (const auto &[_, syms] : bindings->bindings) {
    assert(!syms.empty());
    if (syms.size() > 1)
      this->checkOverloadSet(syms);
  }
}

void Sema::checkOverloadSet(const std::vector<Symbol> &syms) {
  syms.front().get<Function *>()->is_overloaded = true;

  for (size_t i = 1; i < syms.size(); ++i) {
    Function *fi = syms.at(i).get<Function *>();
    fi->is_overloaded = true;

    for (size_t j = 0; j < i; ++j) {
      Function *fj = syms.at(j).get<Function *>();

      if (!fj->hasSameOverloadSignature(fi))
        continue;

      if (fj->loc.has_value() && fi->loc.has_value()) {
        // Both functions are user provided
        this->diag->error(fj->loc.value(), "redefinition of '{}' with the same overload signature", fj->name);
        this->diag->note(fi->loc.value(), "conflicting definition is here");
      } else {
        // User function conflicts with compiler function -> blame the user
        Function *user = fj->loc.has_value() ? fj : fi;
        Function *compiler = fj->loc.has_value() ? fi : fj;

        this->diag->error(user->loc.value(),
                           "'{}' conflicts with a compiler-provided function with signature '{}'",
                           user->name, compiler->signatureString());
      }
    }
  }
}

void Sema::checkFunctionBodies(SourceUnit *unit) {
  for (SourceItem &item : unit->items) {
    if (item.getTag() == SourceItem::Tag::Function) {
      Function *func = item.get<std::unique_ptr<Function>>().get();

      if (func->receiver != nullptr) {
        const Type *type = func->receiver->type;

        if (type != nullptr) {
          // Should check whether errors were already reported on the receiver
          if (type->isPointerType() && type->toPointerType()->isOwning()) {
            this->diag->error(func->receiver->type_name->range,
                             "receiver type cannot be an owning pointer (have '{}')", type->toString());
          } else if (!type->isPointerType() && !type->isTrivial(this->type_infos)) {
            this->diag->error(func->receiver->type_name->range,
                             "receiver type must be trivial (have '{}')", type->toString());
          }
        }
      }

      if (func->body != nullptr) {
        if (func->is_extern) {
          this->diag->error(func->loc.value(), "extern functions must not have an implementation");
        }

        std::vector<const Stmt *> break_targets;
        StmtContext ctx(unit, func->scope.get(), func, &break_targets);
        this->typeCheck(func->body.get(), ctx);
      } else if (!func->is_extern) {
        this->diag->error(func->loc.value(), "missing implementation for non-extern function");
      }
    }
  }
}

void Sema::checkMain() {
  SourceUnit *entry_unit = this->program->units.at(0).get();
  const std::vector<Symbol> *syms = entry_unit->scope->lookup("main");

  if (syms == nullptr)
    this->diag->error("missing definition of 'main'");

  const Symbol &front = syms->front();

  if (front.getTag() != Symbol::Tag::Function)
    return this->diag->error(front.getDeclLoc(), "'main' must be a function");

  if (syms->size() > 1)
    return this->diag->error(front.getDeclLoc(), "'main' cannot be overloaded");

  if (!front.get<Function *>()->hasMainSignature())
    return this->diag->error(front.getDeclLoc(), "invalid signature for 'main'");

  front.get<Function *>()->is_main = true;
}

void Sema::checkUnusedImports() {
  for (auto &unit : this->program->units) {
    for (const Import &import : unit->imports) {
      if (import.used)
        continue;

      this->diag->warning(import.range, "unused import");
    }
  }
}

const Type *Sema::resolve(const TypeName *type_name, TypeNameContext &ctx) {
  switch (type_name->tag) {
  case TypeName::Tag::ElementaryTypeName:    return this->resolve(type_name->toElementaryTypeName(), ctx);
  case TypeName::Tag::PointerTypeName:       return this->resolve(type_name->toPointerTypeName(), ctx);
  case TypeName::Tag::ArrayTypeName:         return this->resolve(type_name->toArrayTypeName(), ctx);
  case TypeName::Tag::SliceTypeName:         return this->resolve(type_name->toSliceTypeName(), ctx);
  case TypeName::Tag::IdentifierTypeName:    return this->resolve(type_name->toIdentifierTypeName(), ctx);
  case TypeName::Tag::ParenthesizedTypeName: return this->resolve(type_name->toParenthesizedTypeName(), ctx);
  }

  assert(false);
}

const Type *Sema::resolve(const ElementaryTypeName *type_name, TypeNameContext &) {
  switch (type_name->keyword) {
  case Keyword::Bool:   return TypeProvider::Bool();
  case Keyword::Char:   return TypeProvider::Char();
  case Keyword::Int8:   return TypeProvider::Int8();
  case Keyword::Int16:  return TypeProvider::Int16();
  case Keyword::Int32:  return TypeProvider::Int32();
  case Keyword::Int64:  return TypeProvider::Int64();
  case Keyword::Int:    return TypeProvider::Int64();
  case Keyword::UInt8:  return TypeProvider::UInt8();
  case Keyword::UInt16: return TypeProvider::UInt16();
  case Keyword::UInt32: return TypeProvider::UInt32();
  case Keyword::UInt64: return TypeProvider::UInt64();
  case Keyword::UInt:   return TypeProvider::UInt64();
  case Keyword::Str:    return TypeProvider::Str();
  case Keyword::String: return TypeProvider::String();
  default:
    assert(false && "keyword is not an elementary type name");
  }
}

const Type *Sema::resolve(const PointerTypeName *type_name, TypeNameContext &ctx) {
  const Type *pointee = this->resolve(type_name->pointee.get(), ctx);
  return pointee != nullptr ? TypeProvider::Pointer(pointee, type_name->is_owning) : nullptr;
}

const Type *Sema::resolve(const ArrayTypeName *type_name, TypeNameContext &ctx) {
  const Type *element = this->resolve(type_name->element.get(), ctx);

  if (element == nullptr)
    return nullptr;

  Expr *length = type_name->length.get();

  if (length == nullptr)
    return this->ensureTypeRegistered(TypeProvider::Array(element));

  ExprContext expr_ctx(ctx.unit, ctx.scope);
  this->typeCheck(length, expr_ctx);

  if (!length->isAnnotated())
    return nullptr;

  const Constant *constant = length->getConstant();

  if (constant == nullptr || !constant->isIntegerConstant()) {
    this->diag->error(length->range, "array length must be an integer constant");
    return nullptr;
  }

  const IntegerConstant *length_constant = constant->toIntegerConstant();

  if (length_constant->value <= 0) {
    this->diag->error(length->range, "invalid array length {}", length_constant->value.get_str());
    return nullptr;
  }

  if (!length_constant->value.fits_ulong_p()) {
    this->diag->error(length->range, "array length {} is too large", length_constant->value.get_str());
    return nullptr;
  }

  return this->ensureTypeRegistered(TypeProvider::Array(element, length_constant->value.get_ui()));
}

const Type *Sema::resolve(const SliceTypeName *type_name, TypeNameContext &ctx) {
  const Type *element = this->resolve(type_name->element.get(), ctx);
  return element != nullptr ? this->ensureTypeRegistered(TypeProvider::Slice(element)) : nullptr;
}

const Type *Sema::resolve(const IdentifierTypeName *type_name, TypeNameContext &ctx) {
  const std::vector<Symbol> *syms = this->lookupSymbol(type_name->name, ctx.scope, ctx.unit);

  if (syms == nullptr) {
    this->diag->error(type_name->range, "use of undeclared identifier '{}'", type_name->name);
    return nullptr;
  }

  const Symbol &first = syms->front();

  switch (first.getTag()) {
  case Symbol::Tag::Struct:
    return TypeProvider::Struct(first.get<Struct *>());
  case Symbol::Tag::Variant:
    return TypeProvider::Variant(first.get<Variant *>());
  case Symbol::Tag::Enum:
    return TypeProvider::Enum(first.get<Enum *>());
  case Symbol::Tag::Function:
  case Symbol::Tag::Parameter:
  case Symbol::Tag::Variable:
  case Symbol::Tag::StructField: {
    this->diag->error(type_name->range, "'{}' is not a type", type_name->name);
    return nullptr;
  }
  case Symbol::Tag::EnumValue:
  case Symbol::Tag::Property:
    assert(false);
  }

  assert(false);
}

const Type *Sema::resolve(const ParenthesizedTypeName *type_name, TypeNameContext &ctx) {
  return this->resolve(type_name->type_name.get(), ctx);
}

void Sema::typeCheck(Stmt *stmt, StmtContext &ctx) {
  switch (stmt->tag) {
  case Stmt::Tag::BlockStmt:       return this->typeCheck(stmt->toBlockStmt(), ctx);
  case Stmt::Tag::IfStmt:          return this->typeCheck(stmt->toIfStmt(), ctx);
  case Stmt::Tag::ForStmt:         return this->typeCheck(stmt->toForStmt(), ctx);
  case Stmt::Tag::WhileStmt:       return this->typeCheck(stmt->toWhileStmt(), ctx);
  case Stmt::Tag::LabeledStmt:     return this->typeCheck(stmt->toLabeledStmt(), ctx);
  case Stmt::Tag::SwitchStmt:      return this->typeCheck(stmt->toSwitchStmt(), ctx);
  case Stmt::Tag::FallthroughStmt: return this->typeCheck(stmt->toFallthroughStmt(), ctx);
  case Stmt::Tag::ExprStmt:        return this->typeCheck(stmt->toExprStmt(), ctx);
  case Stmt::Tag::SkipStmt:        return this->typeCheck(stmt->toSkipStmt(), ctx);
  case Stmt::Tag::BreakStmt:       return this->typeCheck(stmt->toBreakStmt(), ctx);
  case Stmt::Tag::ContinueStmt:    return this->typeCheck(stmt->toContinueStmt(), ctx);
  case Stmt::Tag::VarDeclStmt:     return this->typeCheck(stmt->toVarDeclStmt(), ctx);
  case Stmt::Tag::ReturnStmt:      return this->typeCheck(stmt->toReturnStmt(), ctx);
  }

  assert(false);
}

void Sema::typeCheck(BlockStmt *stmt, StmtContext &ctx) {
  for (std::unique_ptr<Stmt> &s : stmt->stmts) {
    StmtContext new_ctx(ctx.unit, stmt->scope.get(), ctx.func, ctx.break_targets);
    this->typeCheck(s.get(), new_ctx);
  }
}

void Sema::typeCheck(IfStmt *stmt, StmtContext &ctx) {
  ExprContext expr_ctx(ctx.unit, ctx.scope);
  this->typeCheck(stmt->cond.get(), expr_ctx);

  if (stmt->cond->isAnnotated())
    this->expectType(stmt->cond.get(), TypeProvider::Bool(), nullptr);

  this->typeCheck(stmt->then.get(), ctx);

  if (stmt->else_ != nullptr)
    this->typeCheck(stmt->else_.get(), ctx);
}

void Sema::typeCheck(ForStmt *stmt, StmtContext &ctx) {
  StmtContext new_ctx(ctx.unit, stmt->scope.get(), ctx.func, ctx.break_targets);
  ExprContext expr_ctx(ctx.unit, new_ctx.scope);

  if (stmt->init != nullptr)
    this->typeCheck(stmt->init.get(), new_ctx);

  if (stmt->cond != nullptr) {
    this->typeCheck(stmt->cond.get(), expr_ctx);

    if (stmt->cond->isAnnotated())
      this->expectType(stmt->cond.get(), TypeProvider::Bool(), nullptr);
  }

  if (stmt->step != nullptr) {
    ExprResult res = this->resolve(stmt->step.get(), expr_ctx);

    if (res == ExprResult::Expr && stmt->step->isUntyped())
      this->tryTypeExpr(stmt->step.get());
  }

  new_ctx.break_targets->push_back(stmt);
  this->typeCheck(stmt->body.get(), new_ctx);
  new_ctx.break_targets->pop_back();
}

void Sema::typeCheck(WhileStmt *stmt, StmtContext &ctx) {
  ExprContext expr_ctx(ctx.unit, ctx.scope);
  this->typeCheck(stmt->cond.get(), expr_ctx);

  if (stmt->cond->isAnnotated())
    this->expectType(stmt->cond.get(), TypeProvider::Bool(), nullptr);

  ctx.break_targets->push_back(stmt);
  this->typeCheck(stmt->body.get(), ctx);
  ctx.break_targets->pop_back();
}

void Sema::typeCheck(LabeledStmt *stmt, StmtContext &ctx) {
  if (stmt->stmt->isForStmt() || stmt->stmt->isWhileStmt()) {
   ctx.break_targets->push_back(stmt);
    this->typeCheck(stmt->stmt.get(), ctx);
   ctx.break_targets->pop_back();
  }
}

void Sema::typeCheck(SwitchStmt *stmt, StmtContext &ctx) {
  if (stmt->cond != nullptr) {
    ExprContext expr_ctx(ctx.unit, ctx.scope);
    this->typeCheckNoContext(stmt->cond.get(), expr_ctx);
  }

  bool seen_default = false;

  for (const SwitchClause &clause : stmt->clauses) {
    if (clause.isDefault()) {
      if (seen_default) {
        this->diag->error(clause.label_loc, "duplicate default clause");
        continue;
      }

      seen_default = true;

      if (clause.missingBody())
        this->diag->error(clause.label_loc, "default clause must contain a body");
    }
  }

  if (!stmt->clauses.empty()
      && !stmt->clauses.back().isDefault()
      && stmt->clauses.back().missingBody()) {
    this->diag->error(stmt->clauses.back().label_loc, "last case clause must contain a body (can't fallthrough to void)");
  }

  if (stmt->cond == nullptr)
    return this->typeCheckExpressionlessSwitch(stmt, ctx);

  if (!stmt->cond->isAnnotated() || !stmt->cond->isTyped())
    return this->typeCheckInvalidSwitch(stmt, ctx);

  const Type *type = stmt->cond->getType();

  if (type->isVariantType())
    return this->typeCheckVariantSwitch(stmt, ctx, seen_default);

  if (type->isValue() && !type->isPointerType())
    return this->typeCheckValueSwitch(stmt, ctx, seen_default);

  this->diag->error(stmt->cond->range, "invalid switch condition type '{}'", type->toString());

  this->typeCheckInvalidSwitch(stmt, ctx);
}

void Sema::typeCheckExpressionlessSwitch(SwitchStmt *stmt, StmtContext &ctx) {
  for (SwitchClause &clause : stmt->clauses) {
    if (!clause.isDefault()) {
      ExprContext expr_ctx(ctx.unit, ctx.scope);
      this->typeCheck(clause.label.get(), expr_ctx);

      if (clause.label->isAnnotated())
        this->expectType(clause.label.get(), TypeProvider::Bool(), nullptr);
    }

    this->typeCheckSwitchClauseBody(clause, stmt->clauses, ctx);
  }
}

void Sema::typeCheckVariantSwitch(SwitchStmt *stmt, StmtContext &ctx, bool seen_default) {
  const VariantType *variant = stmt->cond->getType()->toVariantType();

  std::unordered_set<const Type *> seen_types;

  for (SwitchClause &clause : stmt->clauses) {
    if (!clause.isDefault()) {
      Expr *label = clause.label.get();
      ExprContext expr_ctx(ctx.unit, ctx.scope);
      std::optional<const Type *> type = this->tryResolveAsTypeName(label, expr_ctx);

      if (!type.has_value())
        continue;

      if (type.value() == nullptr) {
        // Fallback to resolving as an expression
        ExprContext expr_ctx(ctx.unit, ctx.scope);
        this->typeCheck(label, expr_ctx);

        if (label->isAnnotated())
          this->diag->error(label->range, "case label in variant switch must be a type");

        continue;
      }

      VariantAlternative *alternative = variant->lookupAlternative(type.value());

      if (alternative == nullptr) {
        this->diag->error(label->range, "type '{}' is not an alternative of '{}'",
                          type.value()->toString(), variant->toString());
        continue;
      }

      if (seen_types.count(type.value())) {
        this->diag->error(label->range, "duplicate case label '{}'", type.value()->toString());
        continue;
      }

      seen_types.insert(type.value());
      clause.alternative = alternative;
    }

    this->typeCheckSwitchClauseBody(clause, stmt->clauses, ctx);
  }

  if (!seen_default) {
    std::vector<const Type *> missing;

    const std::vector<VariantAlternative> &alts = variant->getVariant()->alternatives;

    for (const VariantAlternative &alt : alts) {
      if (seen_types.count(alt.type) == 0)
        missing.push_back(alt.type);
    }

    if (!missing.empty()) {
      std::string msg = "non-exhaustive switch, missing cases: ";

      for (size_t i = 0, n = missing.size(); i < n; ++i) {
        msg += missing[i]->toString();

        if (i < n - 1)
          msg += ", ";
      }

      this->diag->warning(stmt->range.getBegin(),"{}", msg);
    }
  }
}

void Sema::typeCheckValueSwitch(SwitchStmt *stmt, StmtContext &ctx, bool seen_default) {
  const Type *type = stmt->cond->getType();

  std::unordered_set<const Constant *> seen_values;

  for (SwitchClause &clause : stmt->clauses) {
    if (!clause.isDefault()) {
      ExprContext expr_ctx(ctx.unit, ctx.scope);
      this->typeCheck(clause.label.get(), expr_ctx);

      if (!clause.label->isAnnotated())
        continue;

      if (!this->expectType(clause.label.get(), type, &stmt->cond->range))
        continue;

      if (!clause.label->isConstant()) {
        this->diag->error(clause.label->range, "expression must be a constant");
        continue;
      }

      const Constant *constant = clause.label->getConstant();

      if (seen_values.count(constant) > 0) {
        this->diag->error(clause.label->range, "duplicate case label {}", constant->toString());
        continue;
      }

      seen_values.insert(constant);
    }

    this->typeCheckSwitchClauseBody(clause, stmt->clauses, ctx);
  }

  if (!seen_default && type->isEnumType()) {
    std::vector<const EnumConstant *> missing;

    std::vector<EnumValue> &members = type->toEnumType()->getEnum()->values;

    for (EnumValue &value : members) {
      const EnumConstant *constant = ConstantProvider::Enum(&value);

      if (seen_values.count(constant) == 0)
        missing.push_back(constant);
    }

    if (!missing.empty()) {
      std::string msg = "non-exhaustive switch, missing cases: ";

      for (size_t i = 0, n = missing.size(); i < n; ++i) {
        msg += missing[i]->toString();

        if (i < n - 1)
          msg += ", ";
      }

      this->diag->warning(stmt->range.getBegin(),"{}", msg);
    }
  }
}

void Sema::typeCheckInvalidSwitch(SwitchStmt *stmt, StmtContext &ctx) {
  for (SwitchClause &clause : stmt->clauses) {
    ExprContext expr_ctx(ctx.unit, ctx.scope);
    if (!clause.isDefault() && this->tryResolveAsTypeName(clause.label.get(), expr_ctx) == nullptr)
      this->typeCheck(clause.label.get(), expr_ctx);

    this->typeCheckSwitchClauseBody(clause, stmt->clauses, ctx);
  }
}

void Sema::typeCheckSwitchClauseBody(SwitchClause &clause, std::vector<SwitchClause> &clauses, StmtContext &ctx) {
  bool is_last_clause = &clause == &clauses.back();

  for (std::unique_ptr<Stmt> &stmt : clause.body) {
    if (&stmt == &clause.body.back() && stmt.get()->isFallthroughStmt()) {
      if (is_last_clause) {
        this->diag->error(stmt.get()->range, "cannot fallthrough last case clause");
      }

      break;
    }

    this->typeCheck(stmt.get(), ctx);
  }
}

void Sema::typeCheck(FallthroughStmt *stmt, StmtContext &ctx) {
  this->diag->error(stmt->range, "fallthrough statement must be the last statement in a switch case body");
}

void Sema::typeCheck(ExprStmt *stmt, StmtContext &ctx) {
  ExprContext expr_ctx(ctx.unit, ctx.scope);
  ExprResult res = this->resolve(stmt->expr.get(), expr_ctx);

  if (res == ExprResult::Expr && stmt->expr->isUntyped())
    this->tryTypeExpr(stmt->expr.get());
}

void Sema::typeCheck(SkipStmt *, StmtContext &) {
  // Nothing to do
  return;
}

void Sema::typeCheck(BreakStmt *stmt, StmtContext &ctx) {
  this->typeCheckBreakOrContinue(stmt, ctx, stmt->label, "break");
}

void Sema::typeCheck(ContinueStmt *stmt, StmtContext &ctx) {
  this->typeCheckBreakOrContinue(stmt, ctx, stmt->label, "continue");
}

void Sema::typeCheckBreakOrContinue(Stmt *stmt, StmtContext &ctx, const std::optional<std::string> &label, std::string_view kind) {
  if (ctx.break_targets->empty()) {
    this->diag->error(stmt->range, "illegal {} statement", kind);
  } else if (label.has_value()) {
    for (auto it = ctx.break_targets->rbegin(); it != ctx.break_targets->rend(); ++it) {
      const Stmt *s = *it;

      if (s->isLabeledStmt() && s->toLabeledStmt()->label == label.value())
        return;
    }

    this->diag->error(stmt->range, "cannot find label '{}'", label.value());
  }
}

void Sema::typeCheck(VarDeclStmt *stmt, StmtContext &ctx) {
  const std::vector<std::unique_ptr<Variable>> &vars = stmt->vars;

  if (stmt->init == nullptr) {
    // If the initializer expression is missing, all types must be specified
    for (const std::unique_ptr<Variable> &var : vars) {
      if (var->type_name == nullptr) {
        this->diag->error(var->loc, "missing type name");
      } else {
        TypeNameContext type_ctx(ctx.unit, ctx.scope);
        const Type *resolved = this->resolve(var->type_name.get(), type_ctx);

        if (resolved != nullptr)
          var->type = resolved;
      }

      this->bindSymbol(var->name, Symbol(var.get()), ctx.scope, ctx.unit);
    }

    return;
  }

  ExprContext expr_ctx(ctx.unit, ctx.scope);

  if (stmt->init->isListExpr()) {
    if (this->typeCheckListComponents(stmt->init->toListExpr(), expr_ctx))
      this->typeCheckVarDecl(stmt, view(stmt->init->toListExpr()->components), ctx);
    return;
  }

  ExprResult res = this->resolve(stmt->init.get(), expr_ctx);

  switch (res) {
  case ExprResult::PureStmt:
    return this->typeCheckVarDecl(stmt, {}, ctx);
  case ExprResult::Expr:
    return this->typeCheckVarDecl(stmt, {stmt->init.get()}, ctx);
  case ExprResult::MultiReturnFunctionCall:
    return this->typeCheckVarDeclCall(stmt, stmt->init->toCallExpr(), ctx);
  case ExprResult::Error:
    return;
  }
}

void Sema::typeCheckVarDecl(VarDeclStmt *stmt, const std::vector<Expr *> &components, StmtContext &ctx) {
  const std::vector<std::unique_ptr<Variable>> &vars = stmt->vars;

  if (components.size() != vars.size()) {
    std::string fmt = "invalid number of values, expected {}, have {}";
    return this->diag->error(stmt->init->range, fmt, vars.size(), components.size());
  }

  for (size_t i = 0, n = components.size(); i < n; ++i) {
    Variable *var = vars[i].get();
    Expr *expr = components[i];
    assert(expr->isAnnotated());

    if (var->type_name != nullptr) {
      TypeNameContext type_ctx(ctx.unit, ctx.scope);
      const Type *target = this->resolve(var->type_name.get(), type_ctx);

      if (target != nullptr) {
        var->type = target;
        this->expectType(expr, target, &var->type_name->range);
      }
    } else if (expr->isTyped() || this->tryTypeExpr(expr)) {
      var->type = expr->getType();
    }

    this->bindSymbol(var->name, Symbol(var), ctx.scope, ctx.unit);
  }
}

void Sema::typeCheckVarDeclCall(VarDeclStmt *stmt, const CallExpr *call, StmtContext &ctx) {
  const std::vector<Parameter> &ret_params = call->call_kind.value().get<FunctionCall>().func->ret_params;
  assert(ret_params.size() > 1);

  const std::vector<std::unique_ptr<Variable>> &vars = stmt->vars;

  if (vars.size() != ret_params.size()) {
    std::string fmt = "invalid number of values, expected {}, have {}";
    return this->diag->error(stmt->init->range, fmt, vars.size(), ret_params.size());
  }

  for (size_t i = 0, n = vars.size(); i < n; ++i) {
    Variable *var = vars[i].get();
    const Type *source = ret_params[i].type;

    if (var->type_name == nullptr) {
      var->type = source;
    } else {
      TypeNameContext type_ctx(ctx.unit, ctx.scope);
      const Type *target = this->resolve(var->type_name.get(), type_ctx);

      if (target == nullptr)
        continue;

      var->type = target;

      if (!source->isImplicitlyConvertibleTo(target)) {
        std::string fmt = "{} component '{}' of multi-return function call is"
                          " not implicitly convertible to expected type '{}'";
        this->diag->error(stmt->init->range, fmt, ordinal(i + 1), source->toString(), target->toString());
        this->diag->note(var->type_name->range, "expected '{}' due to this", target->toString());
        return;
      }
    }
  }

  for (const std::unique_ptr<Variable> &var : vars)
    this->bindSymbol(var->name, Symbol(var.get()), ctx.scope, ctx.unit);
}

void Sema::typeCheck(ReturnStmt *stmt, StmtContext &ctx) {
  const std::vector<Parameter> &ret_params = ctx.func->ret_params;

  if (stmt->expr == nullptr) {
    if (!ret_params.empty())
      this->diag->error(stmt->range, "missing expression");

    return;
  }

  ExprContext expr_ctx(ctx.unit, ctx.scope);

  if (stmt->expr->isListExpr()) {
    if (this->typeCheckListComponents(stmt->expr->toListExpr(), expr_ctx))
      this->typeCheckReturn(stmt, view(stmt->expr->toListExpr()->components), ctx);
    return;
  }

  ExprResult res = this->resolve(stmt->expr.get(), expr_ctx);

  switch (res) {
  case ExprResult::PureStmt:
    return this->typeCheckReturn(stmt, {}, ctx);
  case ExprResult::Expr:
    return this->typeCheckReturn(stmt, {stmt->expr.get()}, ctx);
  case ExprResult::MultiReturnFunctionCall:
    return this->typeCheckReturnCall(stmt, stmt->expr->toCallExpr(), ctx);
  case ExprResult::Error:
    return;
  }

  assert(false);
}

void Sema::typeCheckReturn(ReturnStmt *stmt, const std::vector<Expr *> &components, StmtContext &ctx) {
  const std::vector<Parameter> &ret_params = ctx.func->ret_params;

  if (components.size() != ret_params.size()) {
    std::string fmt = "invalid number of returned values, expected {}, have {}";
    return this->diag->error(stmt->expr->range, fmt, ret_params.size(), components.size());
  }

  for (size_t i = 0, n = components.size(); i < n; ++i) {
    assert(components[i]->isAnnotated());
    const Type *target = ret_params[i].type;

    if (target != nullptr)
      this->expectType(components[i], target, &ret_params[i].type_name->range);
  }
}

void Sema::typeCheckReturnCall(ReturnStmt *stmt, const CallExpr *call, StmtContext &ctx) {
  const std::vector<Parameter> &call_rets = call->call_kind.value().get<FunctionCall>().func->ret_params;
  assert(call_rets.size() > 1);

  const std::vector<Parameter> &ret_params = ctx.func->ret_params;

  if (call_rets.size() != ret_params.size()) {
    std::string fmt = "invalid number of returned values, expected {}, have {}";
    return this->diag->error(stmt->expr->range, fmt, ret_params.size(), call_rets.size());
  }

  for (size_t i = 0, n = call_rets.size(); i < n; ++i) {
    const Parameter &ret_param = ret_params[i];

    const Type *target = ret_param.type;

    if (target == nullptr)
      continue;

    if (!call_rets[i].type->isImplicitlyConvertibleTo(target)) {
      this->diag->error(stmt->expr->range,
                        "{} component '{}' of multi-return function call"
                        " is not implicitly convertible to expected type '{}'",
                        ordinal(i + 1), call_rets[i].type->toString(), target->toString());
      this->diag->note(ret_param.type_name->range,
                       "expecting '{}' due to this", target->toString());
      return;
    }
  }
}

std::optional<const Type *> Sema::tryResolveAsTypeName(const Expr *expr, ExprContext &ctx) {
  if (expr->isUnaryExpr() && expr->toUnaryExpr()->op == UnaryOp::Indirection) {
    std::optional<const Type *> pointee = this->tryResolveAsTypeName(expr->toUnaryExpr()->operand.get(), ctx);

    if (!pointee.has_value() || pointee.value() == nullptr)
      return pointee;

    return TypeProvider::Pointer(pointee.value(), false);
  }

  if (expr->isIdentifierExpr()) {
    const std::vector<Symbol> *syms = this->lookupSymbol(expr->toIdentifierExpr()->name, ctx.scope, ctx.unit);

    if (syms == nullptr)
      return nullptr;

    const Symbol &front = syms->front();

    switch (front.getTag()) {
    case Symbol::Tag::Struct:
      return TypeProvider::Struct(front.get<Struct *>());
    case Symbol::Tag::Variant:
      return TypeProvider::Variant(front.get<Variant *>());
    case Symbol::Tag::Enum:
      return TypeProvider::Enum(front.get<Enum *>());
    case Symbol::Tag::Function:
    case Symbol::Tag::Parameter:
    case Symbol::Tag::Variable:
      return nullptr;
    case Symbol::Tag::StructField:
    case Symbol::Tag::EnumValue:
    case Symbol::Tag::Property:
      assert(false);
    }

    assert(false);
  }

  if (expr->isParenExpr())
    return this->tryResolveAsTypeName(expr->toParenExpr()->expr.get(), ctx);

  if (expr->isTypeNameExpr()) {
    TypeNameContext type_ctx(ctx.unit, ctx.scope);
    const Type *type = this->resolve(expr->toTypeNameExpr()->type_name.get(), type_ctx);
    return type == nullptr ? std::nullopt : std::optional<const Type *>(type);
  }

  return nullptr;
}

bool Sema::expectType(Expr *expr, const Type *target, const SourceRange *ctx_range) {
  tl::expected<AssignInfo, ErrorInfo> res = this->tryAssignExprTo(expr, target);

  if (!res) {
    this->diag->error(res.error().src_info, "{}", res.error().message);

    for (const ErrorInfo::NoteInfo &note_info : res.error().note_info)
      this->diag->note(note_info.src_info, "{}", note_info.message);

    if (ctx_range != nullptr)
      this->diag->note(*ctx_range, "expecting '{}' due to this", target->toString());

    return false;
  } else {
    this->assignExprTo(expr, res.value().target);
    return true;
  }
}

tl::expected<Sema::AssignInfo, Sema::ErrorInfo> Sema::tryAssignExprTo(const Expr *expr, const Type *target) {
  const TypeState &type_state = expr->anno.value().type_state;

  switch (type_state.getTag()) {
  case TypeState::Tag::Typed: {
    if (!expr->getType()->isImplicitlyConvertibleTo(target)) {
      ErrorInfo info(expr->range, "'{}' is not implicitly convertible to expected type '{}'",
                    expr->getType()->toString(), target->toString());
      return tl::unexpected(std::move(info));
    }

    return AssignInfo(expr->getType(), expr->getType()->isEqualTo(target) ? ConvRank::Exact : ConvRank::Implicit);
  }
  case TypeState::Tag::UntypedIntegerConstant: {
    if (target->isIntegerType()) {
      if (!expr->getConstant()->toIntegerConstant()->fitsIntegerType(target->toIntegerType())) {
        ErrorInfo info(expr->range, "integer constant {} is out of range for expected type '{}'",
                       expr->getConstant()->toString(), target->toString());
        return tl::unexpected(std::move(info));
      }

      return AssignInfo(target, ConvRank::Exact);
    }

    break;
  }
  case TypeState::Tag::UntypedStringConstant: {
    if (target->isStringType() || target->isStrType())
      return AssignInfo(target, ConvRank::Exact);

    if (target->isArrayType() && target->toArrayType()->getElement()->isCharType()) {
      if (target->toArrayType()->isDynamic())
        return AssignInfo(target, ConvRank::Exact);

      if (target->toArrayType()->getLength() != expr->getConstant()->toStringConstant()->value.size()) {
        ErrorInfo info(expr->range, "string constant of {} characters cannot be assigned to expected type '{}'",
                       expr->getConstant()->toStringConstant()->value.size(), target->toString());
        return tl::unexpected(std::move(info));
      }

      return AssignInfo(target, ConvRank::Exact);
    }

    if (target->isSliceType() && target->toSliceType()->getElement()->isCharType()) {
      // target = [*]char
      // intermediate target = [N]char
      const ArrayType *array = TypeProvider::Array(TypeProvider::Char(), expr->getConstant()->toStringConstant()->value.size());
      return AssignInfo(this->ensureTypeRegistered(array), ConvRank::Implicit);
    }

    break;
  }
  case TypeState::Tag::UntypedNullConstant: {
    if (target->isPointerType())
      return AssignInfo(target, ConvRank::Exact);

    break;
  }
  case TypeState::Tag::UntypedCompositeLiteral: {
    if (target->isArrayType() || target->isSliceType()) {
      const Type *element = (target->isArrayType()
                             ? target->toArrayType()->getElement()
                             : target->toSliceType()->getElement());

      std::vector<Expr *> components = view(expr->unparenthesize()->toCompositeLiteralExpr()->components);

      for (size_t i = 0, n = components.size(); i < n; ++i) {
        tl::expected<AssignInfo, ErrorInfo> res = this->tryAssignExprTo(components[i], element);

        if (!res) {
          std::string fmt = "in {} component of composite literal of expected type '{}'";
          res.error().note_info.emplace_back(expr->range, fmt, ordinal(i + 1), target->toString());
          return res;
        }
      }

      if (target->isArrayType() && !target->toArrayType()->isDynamic()
          && target->toArrayType()->getLength() != components.size()) {
        ErrorInfo info(expr->unparenthesize()->range,
                       "composite literal has {} element{} but expected {} for type '{}'",
                       components.size(), components.size() == 1 ? "" : "s",
                       target->toArrayType()->getLength(), target->toString());
        return tl::unexpected(info);
      }

      if (target->isArrayType() || (target->isSliceType() && components.empty()))
        return AssignInfo(target, ConvRank::Exact);

      return AssignInfo(this->ensureTypeRegistered(TypeProvider::Array(element, components.size())), ConvRank::Implicit);
    }

    if (target->isStructType()) {
      Struct *struct_ = target->toStructType()->getStruct();

      std::vector<Expr *> components = view(expr->unparenthesize()->toCompositeLiteralExpr()->components);

      if (components.size() != struct_->fields.size()) {
        ErrorInfo info(expr->unparenthesize()->range,
                       "composite literal has {} element{} but expected {} for type '{}'",
                       components.size(), components.size() == 1 ? "" : "s",
                       struct_->fields.size(), target->toString());
        return tl::unexpected(std::move(info));
      }

      for (size_t i = 0, n = components.size(); i < n; ++i) {
        const Type *field_type = struct_->fields[i].type;
        tl::expected<AssignInfo, ErrorInfo> res = this->tryAssignExprTo(components[i], field_type);

        if (!res) {
          res.error().note_info.emplace_back(components[i]->range, "in initializer for field '{}' of type '{}' of struct '{}'",
                                             struct_->fields[i].name, field_type->toString(), struct_->name);
          return res;
        }
      }

      return AssignInfo(target, ConvRank::Exact);
    }

    break;
  }
  }

  if (target->isVariantType()) {
    std::vector<VariantAlternative> &alternatives = target->toVariantType()->getVariant()->alternatives;

    std::vector<const Type *> candidates;

    for (const VariantAlternative &alternative : alternatives) {
      tl::expected<AssignInfo, ErrorInfo> res = this->tryAssignExprTo(expr, alternative.type);

      if (res && res.value().rank == ConvRank::Exact)
        candidates.push_back(alternative.type);
    }

    if (candidates.empty()) {
      std::string fmt = "{} is not assignable to expected type '{}'; it is not assignable to any of its alternatives";
      ErrorInfo err(expr->range, fmt, type_state.untypedDesc(), target->toString());
      return tl::unexpected(std::move(err));
    } else if (candidates.size() > 1) {
      std::string alt_list;

      for (size_t i = 0, n = candidates.size(); i < n; ++i) {
        alt_list += "'" + candidates[i]->toString() + "'";

        if (i < n - 1)
          alt_list += ", ";
      }

      std::string fmt = "{} is not assignable to expected type '{}'; it is assignable to multiple of its alternatives ({})";
      ErrorInfo err(expr->range, fmt, type_state.untypedDesc(), target->toString(), alt_list);
      return tl::unexpected(std::move(err));
    } else {
      return AssignInfo(candidates.front(), ConvRank::Implicit);
    }
  }

  ErrorInfo err(expr->range, "{} is not assignable to expected type '{}'", type_state.untypedDesc(), target->toString());
  return tl::unexpected(std::move(err));
}

void Sema::assignExprTo(Expr *expr, const Type *type) {
  TypeState &type_state = expr->anno.value().type_state;

  switch (type_state.getTag()) {
  case TypeState::Tag::Typed:
    return;
  case TypeState::Tag::UntypedIntegerConstant:
  case TypeState::Tag::UntypedStringConstant:
  case TypeState::Tag::UntypedNullConstant: {
    type_state = Typed(type);
    return;
  }
  case TypeState::Tag::UntypedCompositeLiteral: {
    std::vector<Expr *> components = view(expr->unparenthesize()->toCompositeLiteralExpr()->components);

    if (!components.empty()) {
      if (type->isArrayType()) {
        const Type *element = type->toArrayType()->getElement();

        for (Expr *component : components)
          this->assignExprTo(component, element);
      } else {
        assert(type->isStructType());
        Struct *struct_ = type->toStructType()->getStruct();

        for (size_t i = 0, n = components.size(); i < n; ++i)
          this->assignExprTo(components[i], struct_->fields[i].type);
      }
    }

    type_state = Typed(type);
    return;
  }
  }

  assert(false);
}

void Sema::typeCheckNoContext(Expr *expr, ExprContext &ctx) {
  this->typeCheck(expr, ctx);

  if (expr->isAnnotated() && !expr->isTyped())
    this->tryTypeExpr(expr);
}

bool Sema::tryTypeExpr(Expr *expr) {
  TypeState &type_state = expr->anno.value().type_state;

  switch (type_state.getTag()) {
  case TypeState::Tag::Typed:
    assert(false && "expression is already typed");
  case TypeState::Tag::UntypedIntegerConstant: {
    const IntegerType *default_type = TypeProvider::Int64();

    if (!expr->getConstant()->toIntegerConstant()->fitsIntegerType(default_type)) {
      this->diag->error(expr->range, "integer constant {} out of range for default type 'int64'",
                        expr->getConstant()->toIntegerConstant()->value.get_str());
      return false;
    }

    this->diag->warning(expr->range, "untyped integer constant defaults to type 'int64'");
    type_state = Typed(default_type);
    return true;
  }
  case TypeState::Tag::UntypedStringConstant: {
    this->diag->warning(expr->range, "untyped string constant defaults to type 'str'");
    type_state = Typed(TypeProvider::Str());
    return true;
  }
  case TypeState::Tag::UntypedCompositeLiteral:
  case TypeState::Tag::UntypedNullConstant: {
    this->diag->error(expr->range, "cannot determine type of {} in this context", type_state.untypedDesc());
    return false;
  }
  }

  assert(false);
}

void Sema::typeCheck(Expr *expr, ExprContext &ctx) {
  switch (expr->tag) {
  case Expr::Tag::AssignmentExpr:       return this->typeCheck(expr->toAssignmentExpr(), ctx);
  case Expr::Tag::UnaryExpr:            return this->typeCheck(expr->toUnaryExpr(), ctx);
  case Expr::Tag::BinaryExpr:           return this->typeCheck(expr->toBinaryExpr(), ctx);
  case Expr::Tag::CallExpr:             return this->typeCheck(expr->toCallExpr(), ctx);
  case Expr::Tag::IndexAccessExpr:      return this->typeCheck(expr->toIndexAccessExpr(), ctx);
  case Expr::Tag::MemberAccessExpr:     return this->typeCheck(expr->toMemberAccessExpr(), ctx);
  case Expr::Tag::VariantAccessExpr:    return this->typeCheck(expr->toVariantAccessExpr(), ctx);
  case Expr::Tag::TypeTestExpr:         return this->typeCheck(expr->toTypeTestExpr(), ctx);
  case Expr::Tag::TypeCastExpr:         return this->typeCheck(expr->toTypeCastExpr(), ctx);
  case Expr::Tag::IdentifierExpr:       return this->typeCheck(expr->toIdentifierExpr(), ctx);
  case Expr::Tag::ParenExpr:            return this->typeCheck(expr->toParenExpr(), ctx);
  case Expr::Tag::ListExpr:             return this->typeCheck(expr->toListExpr(), ctx);
  case Expr::Tag::CompositeLiteralExpr: return this->typeCheck(expr->toCompositeLiteralExpr(), ctx);
  case Expr::Tag::NewExpr:              return this->typeCheck(expr->toNewExpr(), ctx);
  case Expr::Tag::TypeNameExpr:         return this->typeCheck(expr->toTypeNameExpr(), ctx);
  case Expr::Tag::IntLiteralExpr:       return this->typeCheck(expr->toIntLiteralExpr(), ctx);
  case Expr::Tag::BoolLiteralExpr:      return this->typeCheck(expr->toBoolLiteralExpr(), ctx);
  case Expr::Tag::NullExpr:             return this->typeCheck(expr->toNullExpr(), ctx);
  case Expr::Tag::StringLiteralExpr:    return this->typeCheck(expr->toStringLiteralExpr(), ctx);
  case Expr::Tag::CharLiteralExpr:      return this->typeCheck(expr->toCharLiteralExpr(), ctx);
  }

  assert(false);
}

Sema::ExprResult Sema::resolve(Expr *expr, ExprContext &ctx) {
  switch (expr->tag) {
  case Expr::Tag::AssignmentExpr:
    return this->resolve(expr->toAssignmentExpr(), ctx);
  case Expr::Tag::CallExpr:
    return this->resolve(expr->toCallExpr(), ctx);
  case Expr::Tag::UnaryExpr:
  case Expr::Tag::BinaryExpr:
  case Expr::Tag::IndexAccessExpr:
  case Expr::Tag::MemberAccessExpr:
  case Expr::Tag::VariantAccessExpr:
  case Expr::Tag::TypeTestExpr:
  case Expr::Tag::TypeCastExpr:
  case Expr::Tag::IdentifierExpr:
  case Expr::Tag::ParenExpr:
  case Expr::Tag::ListExpr:
  case Expr::Tag::CompositeLiteralExpr:
  case Expr::Tag::NewExpr:
  case Expr::Tag::TypeNameExpr:
  case Expr::Tag::IntLiteralExpr:
  case Expr::Tag::BoolLiteralExpr:
  case Expr::Tag::NullExpr:
  case Expr::Tag::StringLiteralExpr:
  case Expr::Tag::CharLiteralExpr: {
    this->typeCheck(expr, ctx);
    return expr->isAnnotated() ? ExprResult::Expr : ExprResult::Error;
  }
  }

  assert(false);
}

void Sema::typeCheck(AssignmentExpr *expr, ExprContext &ctx) {
  ExprResult res = this->resolve(expr, ctx);

  switch (res) {
  case ExprResult::PureStmt:
    return this->diag->error(expr->range, "assignment cannot be used as an expression");
  case ExprResult::Expr:
  case ExprResult::MultiReturnFunctionCall:
    assert(false);
  case ExprResult::Error:
    return;
  }

  assert(false);
}

Sema::ExprResult Sema::resolve(AssignmentExpr *expr, ExprContext &ctx) {
  std::vector<Expr *> left_components = expr->left->extractComponents();

  for (Expr *component : left_components) {
    this->typeCheck(component, ctx);

    if (!component->isAnnotated())
      return ExprResult::Error;

    if (!component->anno.value().is_lvalue) {
      this->diag->error(component->range, "expression must be an l-value");
      return ExprResult::Error;
    }
  }

  if (expr->right->isListExpr()) {
    if (this->typeCheckListComponents(expr->right->toListExpr(), ctx))
      return this->resolveAssignment(expr, left_components, view(expr->right->toListExpr()->components), ctx);
    return ExprResult::Error;
  }

  ExprResult res = this->resolve(expr->right.get(), ctx);

  switch (res) {
  case ExprResult::PureStmt:
    return this->resolveAssignment(expr, left_components, {}, ctx);
  case ExprResult::Expr:
    return this->resolveAssignment(expr, left_components, {expr->right.get()}, ctx);
  case ExprResult::MultiReturnFunctionCall:
    return this->resolveAssignmentCall(expr, left_components, expr->right->toCallExpr(), ctx);
  case ExprResult::Error:
    return ExprResult::Error;
  }

  assert(false);
}

Sema::ExprResult Sema::resolveAssignment(AssignmentExpr *expr, const std::vector<Expr *> &left_components, const std::vector<Expr *> &right_components, ExprContext &ctx) {
  if (left_components.size() != right_components.size()) {
    this->diag->error(expr->right->range, "invalid number of values, expected {}, have {}",
                      left_components.size(), right_components.size());
    return ExprResult::Error;
  }

  for (size_t i = 0, n = left_components.size(); i < n; ++i) {
    assert(left_components[i]->isAnnotated() && right_components[i]->isAnnotated());
    const Type *target = left_components[i]->getType();

    if (!this->expectType(right_components[i], target, &left_components[i]->range))
      return ExprResult::Error;
  }

  return ExprResult::PureStmt;
}

Sema::ExprResult Sema::resolveAssignmentCall(AssignmentExpr *expr, const std::vector<Expr *> &left_components, const CallExpr *call, ExprContext &ctx) {
  const std::vector<Parameter> &ret_params = call->call_kind.value().get<FunctionCall>().func->ret_params;
  assert(ret_params.size() > 1);

  if (ret_params.size() != left_components.size()) {
    this->diag->error(expr->right->range, "invalid number of values, expected {}, have {}",
                      left_components.size(), ret_params.size());
    return ExprResult::Error;
  }

  for (size_t i = 0, n = ret_params.size(); i < n; ++i) {
    assert(left_components[i]->isAnnotated());
    const Type *target = left_components[i]->getType();

    if (!ret_params[i].type->isImplicitlyConvertibleTo(target)) {
      this->diag->error(expr->right->range, "{} component '{}' of multi-return function"
                        " call is not implicitly convertible to expected type '{}'",
                        ordinal(i + 1), ret_params[i].type->toString(), target->toString());
      this->diag->note(left_components[i]->range, "expecting '{}' due to this", target->toString());
      return ExprResult::Error;
    }
  }

  return ExprResult::PureStmt;
}

void Sema::typeCheck(UnaryExpr *expr, ExprContext &ctx) {
  Expr *operand = expr->operand.get();

  this->typeCheck(operand, ctx);

  if (!operand->isAnnotated())
    return;

  const ExprAnno &operand_anno = operand->anno.value();

  switch (expr->op) {
  case UnaryOp::Plus: {
    if ((operand->isTyped() && operand->getType()->isIntegerType())
        || (operand->isUntyped() && operand_anno.type_state.is<UntypedIntegerConstant>())) {
      expr->anno = operand_anno;
      return;
    }

    break;
  }
  case UnaryOp::Minus: {
    if (operand->isTyped() && operand->getType()->isIntegerType()) {
      if (!operand->getType()->toIntegerType()->isSigned()) {
        this->diag->error(DiagSourceInfo(expr->op_range.getBegin(), {expr->operand->range}),
                          "negation on unsigned integer is not allowed");
        return;
      }

      const IntegerType *int_type = operand->getType()->toIntegerType();
      const IntegerConstant *new_ = operand->isConstant() ? ConstantProvider::Integer(-operand->getConstant()->toIntegerConstant()->value) : nullptr;

      if (new_ != nullptr && !this->checkIntegerConstantFits(new_, int_type, expr->range))
        return;

      expr->anno = ExprAnno(Typed(int_type), new_, false);
      return;
    }

    if (operand->isUntyped() && operand_anno.type_state.is<UntypedIntegerConstant>()) {
      const Constant *new_ = ConstantProvider::Integer(-operand->getConstant()->toIntegerConstant()->value);
      expr->anno = ExprAnno(UntypedIntegerConstant(), new_, false);
      return;
    }

    break;
  }
  case UnaryOp::Indirection: {
    if (operand->isTyped() && operand->getType()->isPointerType()) {
      expr->anno = ExprAnno(Typed(operand->getType()->toPointerType()->getPointee()), nullptr, true);
      return;
    }

    break;
  }
  case UnaryOp::AddressOf: {
    if (!operand_anno.is_lvalue) {
      this->diag->error(DiagSourceInfo(operand->range), "expression must be an l-value");
      return;
    }

    assert(operand->isTyped());
    expr->anno = ExprAnno(Typed(TypeProvider::Pointer(operand->getType(), false)), nullptr, false);
    return;
  }
  case UnaryOp::Not: {
    if (operand->isTyped() && operand->getType()->isBoolType()) {
      const BoolConstant *new_ = (operand->isConstant()
                                  ? ConstantProvider::Bool(!operand->getConstant()->toBoolConstant()->value)
                                  : nullptr);
      expr->anno = ExprAnno(Typed(TypeProvider::Bool()), new_, false);
      return;
    }

    break;
  }
  case UnaryOp::BitNot: {
    if (operand->isTyped() && operand->getType()->isIntegerType()) {
      const IntegerType *int_type = operand->getType()->toIntegerType();
      const IntegerConstant *new_ = operand->isConstant() ? ConstantProvider::Integer(~operand->getConstant()->toIntegerConstant()->value) : nullptr;

      if (new_ != nullptr && !this->checkIntegerConstantFits(new_, int_type, expr->range))
        return;

      expr->anno = ExprAnno(Typed(int_type), new_, false);
      return;
    }

    if (operand->isUntyped() && operand_anno.type_state.is<UntypedIntegerConstant>()) {
      const Constant *new_ = ConstantProvider::Integer(~operand->getConstant()->toIntegerConstant()->value);
      expr->anno = ExprAnno(UntypedIntegerConstant(), new_, false);
      return;
    }

    break;
  }
  }

  this->diag->error(DiagSourceInfo(expr->op_range.getBegin(), {operand->range}),
                    "invalid operand to unary operator '{}'",
                    unaryOpToString(expr->op));
}

void Sema::typeCheck(BinaryExpr *expr, ExprContext &ctx) {
  Expr *left = expr->left.get();
  Expr *right = expr->right.get();

  this->typeCheck(left, ctx);
  this->typeCheck(right, ctx);

  if (!left->isAnnotated() || !right->isAnnotated())
    return;

  const ExprAnno &left_anno = left->anno.value();
  const ExprAnno &right_anno = right->anno.value();

  switch (expr->op) {
  case BinaryOp::Add:
  case BinaryOp::Sub:
  case BinaryOp::Mul:
  case BinaryOp::Div:
  case BinaryOp::Rem:
  case BinaryOp::BitAnd:
  case BinaryOp::BitOr:
  case BinaryOp::BitXor: {
    const IntegerType *result = nullptr;

    if (left->isTyped() && left->getType()->isIntegerType()
        && right->isTyped() && right->getType()->isEqualTo(left->getType())) {
      result = left->getType()->toIntegerType();
    } else if (left->isTyped() && left->getType()->isIntegerType() && right->isUntyped()) {
      if (!this->expectType(right, left->getType(), &left->range))
        return;
      result = left->getType()->toIntegerType();
    } else if (right->isTyped() && right->getType()->isIntegerType() && left->isUntyped()) {
      if (!this->expectType(left, right->getType(), &right->range))
        return;
      result = right->getType()->toIntegerType();
    }

    if (result != nullptr)
      return this->finishBinary(expr, Typed(result));

    if (left->isUntyped() && left_anno.type_state.is<UntypedIntegerConstant>()
        && right->isUntyped() && right_anno.type_state.is<UntypedIntegerConstant>())
      return this->finishBinary(expr, UntypedIntegerConstant());

    if (expr->op == BinaryOp::Add) {
      // str x str -> string
      tl::expected<AssignInfo, ErrorInfo> left_res = this->tryAssignExprTo(left, TypeProvider::Str());
      tl::expected<AssignInfo, ErrorInfo> right_res = this->tryAssignExprTo(right, TypeProvider::Str());

      if (left_res.has_value() && right_res.has_value()) {
        this->assignExprTo(left, left_res.value().target);
        this->assignExprTo(right, right_res.value().target);
        TypeState result = (left->isConstant() && right->isConstant()
                            ? TypeState(UntypedStringConstant())
                            : Typed(TypeProvider::String()));
        return this->finishBinary(expr, result);
      }
    }

    break;
  }

  case BinaryOp::LT:
  case BinaryOp::LE:
  case BinaryOp::GT:
  case BinaryOp::GE:
  case BinaryOp::EQ:
  case BinaryOp::NE: {
    const IntegerType *result = nullptr;

    if (left->isTyped() && left->getType()->isIntegerType()
        && right->isTyped() && right->getType()->isEqualTo(left->getType())) {
      result = left->getType()->toIntegerType();
    } else if (left->isTyped() && left->getType()->isIntegerType() && right->isUntyped()) {
      if (!this->expectType(right, left->getType(), &left->range))
        return;
      result = left->getType()->toIntegerType();
    } else if (right->isTyped() && right->getType()->isIntegerType() && left->isUntyped()) {
      if (!this->expectType(left, right->getType(), &right->range))
        return;
      result = right->getType()->toIntegerType();
    }

    if (result != nullptr)
      return this->finishBinary(expr, Typed(TypeProvider::Bool()));

    if (left->isUntyped() && left_anno.type_state.is<UntypedIntegerConstant>()
        && right->isUntyped() && right_anno.type_state.is<UntypedIntegerConstant>())
      return this->finishBinary(expr, Typed(TypeProvider::Bool()));

    if (left->isTyped() && left->getType()->isCharType()
        && right->isTyped() && right->getType()->isCharType())
      return this->finishBinary(expr, Typed(TypeProvider::Bool()));

    if (expr->op != BinaryOp::EQ && expr->op != BinaryOp::NE)
      break;

    // bool x bool, T x T where T is an enum type
    if (left->isTyped() && (left->getType()->isBoolType() || left->getType()->isEnumType())
        && right->isTyped() && right->getType()->isEqualTo(left->getType()))
      return this->finishBinary(expr, Typed(TypeProvider::Bool()));

    // *T x *T
    if (left->isTyped() && left->getType()->isPointerType()) {
      const Type *target = TypeProvider::Pointer(left->getType()->toPointerType()->getPointee(), false);
      if (this->expectType(right, target, &left->range))
        return this->finishBinary(expr, Typed(TypeProvider::Bool()));
      return;
    } else if (right->isTyped() && right->getType()->isPointerType()) {
      const Type *target = TypeProvider::Pointer(right->getType()->toPointerType()->getPointee(), false);
      if (this->expectType(left, target, &right->range))
        return this->finishBinary(expr, Typed(TypeProvider::Bool()));
      return;
    } else if (left->isUntyped() && left_anno.type_state.is<UntypedNullConstant>()
               && right->isUntyped() && right_anno.type_state.is<UntypedNullConstant>()) {
      return this->finishBinary(expr, Typed(TypeProvider::Bool()));
    }

    // str x str -> string
    tl::expected<AssignInfo, ErrorInfo> left_res = this->tryAssignExprTo(left, TypeProvider::Str());
    tl::expected<AssignInfo, ErrorInfo> right_res = this->tryAssignExprTo(right, TypeProvider::Str());

    if (left_res.has_value() && right_res.has_value()) {
      this->assignExprTo(left, left_res.value().target);
      this->assignExprTo(right, right_res.value().target);
      return this->finishBinary(expr, Typed(TypeProvider::Bool()));
    }

    break;
  }

  case BinaryOp::And:
  case BinaryOp::Or: {
    if (left->isTyped() && left->getType()->isBoolType()
        && right->isTyped() && right->getType()->isBoolType())
      return this->finishBinary(expr, Typed(TypeProvider::Bool()));

    break;
  }

  case BinaryOp::SHL:
  case BinaryOp::SHR: {
    assert(false && "unimplemented");
  }
  }

  DiagSourceInfo loc(expr->op_range.getBegin(), {expr->left->range, expr->right->range});
  this->diag->error(std::move(loc), "invalid operands to binary operator '{}'", binaryOpToString(expr->op));
}

tl::expected<const Constant *, Sema::ErrorInfo> Sema::evalBinary(const BinaryExpr *expr) {
  Expr *left = expr->left.get();
  Expr *right = expr->right.get();

  auto as_int = [](const Constant *c) -> const mpz_class & { return c->toIntegerConstant()->value; };
  auto as_bool = [](const Constant *c) -> bool { return c->toBoolConstant()->value; };
  auto as_char = [](const Constant *c) -> char { return c->toCharConstant()->value; };
  auto as_string = [](const Constant *c) -> const std::string & { return c->toStringConstant()->value; };

  if (expr->op == BinaryOp::And) {
    if (left->isConstant() && !as_bool(left->getConstant()))
      return ConstantProvider::Bool(false);
    return nullptr;
  }

  if (expr->op == BinaryOp::Or) {
    if (left->isConstant() && as_bool(left->getConstant()))
      return ConstantProvider::Bool(true);
    return nullptr;
  }

  if (!left->isConstant() || !right->isConstant())
    return nullptr;

  const Constant *left_constant = left->getConstant();
  const Constant *right_constant = right->getConstant();

  switch (expr->op) {
  case BinaryOp::Add: {
    if (left_constant->isIntegerConstant())
      return ConstantProvider::Integer(as_int(left_constant) + as_int(right_constant));
    return ConstantProvider::String(as_string(left_constant) + as_string(right_constant));
  }
  case BinaryOp::Sub:
    return ConstantProvider::Integer(as_int(left_constant) - as_int(right_constant));
  case BinaryOp::Mul:
    return ConstantProvider::Integer(as_int(left_constant) * as_int(right_constant));
  case BinaryOp::Div: {
    if (as_int(right_constant) == 0) {
      ErrorInfo info(DiagSourceInfo(expr->op_range.getBegin(), {expr->right->range}), "division by zero");
      return tl::unexpected(std::move(info));
    }

    return ConstantProvider::Integer(as_int(left_constant) / as_int(right_constant));
  }
  case BinaryOp::Rem: {
    if (as_int(right_constant) == 0) {
      ErrorInfo info(DiagSourceInfo(expr->op_range.getBegin(), {expr->right->range}), "remainder by zero");
      return tl::unexpected(std::move(info));
    }

    return ConstantProvider::Integer(as_int(left_constant) % as_int(right_constant));
  }
  case BinaryOp::BitAnd:
    return ConstantProvider::Integer(as_int(left_constant) & as_int(right_constant));
  case BinaryOp::BitOr:
    return ConstantProvider::Integer(as_int(left_constant) | as_int(right_constant));
  case BinaryOp::BitXor:
    return ConstantProvider::Integer(as_int(left_constant) ^ as_int(right_constant));
  case BinaryOp::SHL:
  case BinaryOp::SHR: {
    const mpz_class &shift = as_int(right_constant);

    if (shift < 0) {
      ErrorInfo info(DiagSourceInfo(expr->op_range.getBegin(), {expr->right->range}), "negative shift amount");
      return tl::unexpected(std::move(info));
    }

    if (shift > 10000) {
      ErrorInfo info(DiagSourceInfo(expr->op_range.getBegin(), {expr->right->range}), "shift amount too large");
      return tl::unexpected(std::move(info));
    }

    unsigned amount = shift.get_ui();
    return (expr->op == BinaryOp::SHL
            ? ConstantProvider::Integer(as_int(left_constant) << amount)
            : ConstantProvider::Integer(as_int(left_constant) >> amount));
  }

  case BinaryOp::LT:
  case BinaryOp::LE:
  case BinaryOp::GT:
  case BinaryOp::GE: {
    bool result = false;

    if (left_constant->isIntegerConstant()) {
      const mpz_class &a = as_int(left_constant);
      const mpz_class &b = as_int(right_constant);

      switch (expr->op) {
      case BinaryOp::LT:
        result = a < b;
        break;
      case BinaryOp::LE:
        result = a <= b;
        break;
      case BinaryOp::GT:
        result = a > b;
        break;
      case BinaryOp::GE:
        result = a >= b;
        break;
      default:
        assert(false);
      }
    } else {
      char a = as_char(left_constant);
      char b = as_char(right_constant);

      switch (expr->op) {
      case BinaryOp::LT:
        result = a < b;
        break;
      case BinaryOp::LE:
        result = a <= b;
        break;
      case BinaryOp::GT:
        result = a > b;
        break;
      case BinaryOp::GE:
        result = a >= b;
        break;
      default:
        assert(false);
      }
    }

    return ConstantProvider::Bool(result);
  }
  case BinaryOp::EQ:
  case BinaryOp::NE: {
    // Constants are interned, pointer comparison is valid here.
    bool result = left_constant == right_constant;
    return ConstantProvider::Bool(expr->op == BinaryOp::EQ ? result : !result);
  }
  case BinaryOp::And:
  case BinaryOp::Or:
    assert(false && "already handled");
  }
}

void Sema::finishBinary(BinaryExpr *expr, TypeState result) {
  tl::expected<const Constant *, ErrorInfo> eval_res = this->evalBinary(expr);

  if (!eval_res.has_value())
    // Should not have any notes
    return this->diag->error(eval_res.error().src_info, eval_res.error().message);

  const Constant *constant = eval_res.value();

  if (result.is<Typed>() && constant != nullptr && constant->isIntegerConstant()
      && !this->checkIntegerConstantFits(constant->toIntegerConstant(), result.get<Typed>().type->toIntegerType(), expr->range))
    return;

  expr->anno = ExprAnno(result, constant, false);
}

bool Sema::checkIntegerConstantFits(const IntegerConstant *constant, const IntegerType *type, SourceRange range) {
  if (!constant->fitsIntegerType(type)) {
    this->diag->error(range, "integer constant {} out of range for '{}'",
                      constant->value.get_str(), type->toString());
    return false;
  }

  return true;
}

std::vector<Function *> Sema::symbolsToFunctions(const std::vector<Symbol> &syms) {
  std::vector<Function *> funcs;
  funcs.reserve(syms.size());

  for (const Symbol &sym : syms)
    funcs.push_back(sym.get<Function *>());

  return funcs;
}

void Sema::typeCheck(CallExpr *expr, ExprContext &ctx) {
  ExprResult res = this->resolve(expr, ctx);

  switch (res) {
  case ExprResult::PureStmt:
  case ExprResult::MultiReturnFunctionCall: {
    return this->diag->error(expr->lparen_loc, "called function must return exactly one value in this context");
  }
  case ExprResult::Expr:
  case ExprResult::Error:
    return;
  }

  assert(false);
}

Sema::ExprResult Sema::resolve(CallExpr *expr, ExprContext &ctx) {
  const Expr *callee = expr->callee.get();

  if (callee->isIdentifierExpr()) {
    const IdentifierExpr *id_expr = callee->toIdentifierExpr();

    const std::vector<Symbol> *syms = this->lookupSymbol(id_expr->name, ctx.scope, ctx.unit);

    if (syms != nullptr && syms->front().getTag() == Symbol::Tag::Function) {
      std::vector<Function *> funcs = this->symbolsToFunctions(*syms);

      Function *func = this->resolveFunctionCall(expr, &funcs, ctx, false);
      return this->funcResult(expr, func, false);
    }
  }

  if (callee->isMemberAccessExpr()) {
    const MemberAccessExpr *member_access = callee->toMemberAccessExpr();

    std::optional<const Type *> qualifier = this->tryResolveAsTypeName(member_access->base.get(), ctx);

    if (!qualifier.has_value())
      return ExprResult::Error;

    if (qualifier.value() != nullptr) {
      if (member_access->is_arrow) {
        this->diag->error(DiagSourceInfo(member_access->dot_loc, {member_access->base->range}),
                          "invalid use of '->' for unbound method call for '{}'",
                          qualifier.value()->toString());
        return ExprResult::Error;
      }

      return this->resolveMethodCall(expr, ctx, qualifier.value(), member_access, false);
    }

    this->typeCheckNoContext(member_access->base.get(), ctx);

    if (!member_access->base->isAnnotated() || !member_access->base->isTyped())
      return ExprResult::Error;

    const Type *base_type = member_access->base->getType();

    if (member_access->is_arrow != base_type->isPointerType()) {
      this->diag->error(DiagSourceInfo(member_access->dot_loc, {member_access->base->range}),
                        "invalid use of '{}' for method call on a {}pointer (have '{}')",
                        member_access->is_arrow ? "->" : ".", base_type->isPointerType() ? "" : "non-",
                        base_type->toString());
      return ExprResult::Error;
    }

    const Type *lookup_type = base_type->isPointerType() ? base_type->toPointerType()->getPointee() : base_type;
    return this->resolveMethodCall(expr, ctx, lookup_type, member_access, true);
  }

  if (callee->isNewExpr()) {
    TypeNameContext type_ctx(ctx.unit, ctx.scope);
    const Type *type = this->resolve(callee->toNewExpr()->type_name.get(), type_ctx);

    if (type == nullptr)
      return ExprResult::Error;

    return this->resolveTypeCall(expr, type, ctx);
  }

  std::optional<const Type *> type = this->tryResolveAsTypeName(expr->callee.get(), ctx);

  if (!type.has_value())
    return ExprResult::Error;

  if (type.value() != nullptr)
    return this->resolveTypeCall(expr, type.value(), ctx);

  // Fallback to type checking as an expression
  this->typeCheck(expr->callee.get(), ctx);

  if (expr->callee->isAnnotated())
    this->diag->error(expr->callee->range, "expression is not callable");

  return ExprResult::Error;
}

Function *Sema::resolveFunctionCall(CallExpr *expr, const std::vector<Function *> *funcs, ExprContext &ctx, bool is_bound) {
  if (funcs->size() == 1)
    return this->resolveFunctionCall(expr, funcs->front(), ctx, is_bound);
  else
    return this->resolveOverloadedFunctionCall(expr, funcs, ctx, is_bound);
}

Function *Sema::resolveFunctionCall(CallExpr *expr, Function *func, ExprContext &ctx, bool is_bound) {
  std::vector<const Parameter *> params = func->getCallParams(is_bound);
  std::vector<CallArg> &args = expr->arguments;

  if (func->isStaticMethod() && is_bound) {
    const MemberAccessExpr *member_access = expr->callee->toMemberAccessExpr();
    const Type *base_type = member_access->base->getType();
    const Type *lookup_type = base_type->isPointerType() ? base_type->toPointerType()->getPointee() : base_type;

    this->diag->error(DiagSourceInfo(expr->lparen_loc, {member_access->base->range, member_access->member_range}),
                      "cannot call '{}' on an instance of '{}'; it is a static method",
                      func->name, lookup_type->toString());
    return nullptr;
  }

  if (args.size() != params.size()) {
    std::vector<SourceRange> underlines;
    underlines.reserve(args.size());

    for (const CallArg &arg : args)
      underlines.push_back(arg.value.get()->range);

    this->diag->error(DiagSourceInfo(expr->lparen_loc, std::move(underlines)),
                      "invalid number of arguments to call, expected {}, have {}",
                      params.size(), args.size());
    return nullptr;
  }

  for (size_t i = 0, n = args.size(); i < n; ++i) {
    this->typeCheck(args[i].value.get(), ctx);

    if (params[i]->type == nullptr
        || !args[i].value->isAnnotated()
        || !this->expectType(args[i].value.get(), params[i]->type, (params[i]->type_name ? &params[i]->type_name->range : nullptr)))
      return nullptr;
  }

  this->checkFunctionCallNames(expr, func, is_bound);
  return func;
}

Function *Sema::resolveOverloadedFunctionCall(CallExpr *expr, const std::vector<Function *> *candidates, ExprContext &ctx, bool is_bound) {
  assert(!candidates->empty());
  std::vector<CallArg> &args = expr->arguments;

  for (CallArg &arg : args) {
    this->typeCheck(arg.value.get(), ctx);

    if (!arg.value->isAnnotated())
      return nullptr;
  }

  std::vector<OverloadMatch> matches;

  for (Function *candidate : *candidates) {
    if (std::optional<OverloadMatch> match = this->tryMatchOverload(candidate, args, is_bound))
      matches.push_back(std::move(*match));
  }

  if (matches.empty()) {
    this->diag->error(expr->lparen_loc, "no matching overload for call");
    return nullptr;
  }

  // matches >= 1, try select the best
  const OverloadMatch *best = this->selectBestMatch(matches);

  if (best == nullptr) {
    this->diag->error(expr->lparen_loc, "ambiguous function call");

    for (const OverloadMatch &match : matches) {
      if (!match.func->builtin_kind.has_value())
        this->diag->note(match.func->loc.value(), "candidate:");
      else
        this->diag->note("compiler-provided candidate signature: '{}'", match.func->signatureString());
    }

    return nullptr;
  }

  for (size_t i = 0, n = args.size(); i < n; ++i)
    this->assignExprTo(args[i].value.get(), best->arg_types[i]);

  this->checkFunctionCallNames(expr, best->func, is_bound);
  return best->func;
}

std::optional<Sema::OverloadMatch> Sema::tryMatchOverload(Function *func, const std::vector<CallArg> &args, bool is_bound) {
  if (func->isStaticMethod() && is_bound)
    return std::nullopt;

  std::vector<const Parameter *> params = func->getCallParams(is_bound);

  if (args.size() != params.size())
    return std::nullopt;

  OverloadMatch match(func);

  for (size_t i = 0, n = args.size(); i < n; ++i) {
    const Expr *arg = args[i].value.get();
    const Type *target = params.at(i)->type;

    tl::expected<AssignInfo, ErrorInfo> res = this->tryAssignExprTo(arg, target);

    if (!res)
      return std::nullopt;

    match.ranks.push_back(res.value().rank);
    match.arg_types.push_back(res.value().target);
  }

  return match;
}

const Sema::OverloadMatch *Sema::selectBestMatch(const std::vector<OverloadMatch> &matches) {
  for (const OverloadMatch &match : matches) {
    if (match.beatsAll(matches))
      return &match;
  }

  return nullptr;
}

bool Sema::OverloadMatch::beatsAll(const std::vector<OverloadMatch> &matches) const {
  for (const OverloadMatch &other : matches) {
    if (this != &other && !this->dominates(other))
      return false;
  }

  return true;
}

bool Sema::OverloadMatch::dominates(const OverloadMatch &other) const {
  bool saw_dominating = false;

  for (size_t i = 0; i < this->ranks.size(); ++i) {
    if (this->ranks[i] > other.ranks[i])
      return false;
    if (this->ranks[i] < other.ranks[i])
      saw_dominating = true;
  }

  return saw_dominating;
}

void Sema::checkFunctionCallNames(CallExpr *expr, Function *func, bool is_bound) {
  std::vector<const Parameter *> params = func->getCallParams(is_bound);
  const std::vector<CallArg> &args = expr->arguments;

  for (size_t i = 0, n = args.size(); i < n; ++i) {
    if (!args[i].isNamed())
      continue;

    if (!params[i]->name.has_value()) {
      this->diag->error(args[i].name.value().range, "illegal parameter name");
    } else if (args[i].name.value().text != params[i]->name.value()) {
      this->diag->error(args[i].name.value().range, "invalid parameter name");
    }
  }
}

Sema::ExprResult Sema::funcResult(CallExpr *expr, Function *func, bool is_bound_method_call) {
  if (func == nullptr)
    return ExprResult::Error;

  const std::vector<Parameter> &ret_params = func->ret_params;

  switch (ret_params.size()) {
  case 0: {
    expr->call_kind = FunctionCall(func, is_bound_method_call);
    return ExprResult::PureStmt;
  }
  case 1: {
    const Type *return_type = ret_params.front().type;
    expr->anno = ExprAnno(Typed(return_type), nullptr, false);
    expr->call_kind = FunctionCall(func, is_bound_method_call);
    return ExprResult::Expr;
  }
  default: {
    expr->call_kind = FunctionCall(func, is_bound_method_call);
    return ExprResult::MultiReturnFunctionCall;
  }
  }
}

Sema::ExprResult Sema::resolveMethodCall(CallExpr *expr, ExprContext &ctx, const Type *lookup_type,
                                         const MemberAccessExpr *member_access, bool is_bound) {
  Bindings *members = &this->type_infos.at(lookup_type).members;
  const std::vector<Symbol> *syms = members->lookup(member_access->member_name);

  if (syms == nullptr) {
    DiagSourceInfo src_info(member_access->dot_loc, {member_access->base->range, member_access->member_range});
    this->diag->error(std::move(src_info), "could not find method '{}' in '{}'",
                      member_access->member_name, lookup_type->toString());
    return ExprResult::Error;
  }

  const Symbol &first = syms->front();

  switch (first.getTag()) {
  case Symbol::Tag::Function: {
    std::vector<Function *> funcs = this->symbolsToFunctions(*syms);

    Function *func = this->resolveFunctionCall(expr, &funcs, ctx, is_bound);
    return this->funcResult(expr, func, is_bound);
  }
  case Symbol::Tag::StructField:
  case Symbol::Tag::EnumValue:
  case Symbol::Tag::Property: {
    this->diag->error(member_access->dot_loc,
                      "'{}' is not a method of '{}'", member_access->member_name, lookup_type->toString());
    return ExprResult::Error;
  }
  case Symbol::Tag::Parameter:
  case Symbol::Tag::Variable:
  case Symbol::Tag::Struct:
  case Symbol::Tag::Variant:
  case Symbol::Tag::Enum:
    assert(false && "symbol cannot be a type member");
  }

  assert(false);
}

Sema::ExprResult Sema::resolveTypeCall(CallExpr *expr, const Type *type, ExprContext &ctx) {
  if (type->isValue()) {
    std::vector<CallArg> &args = expr->arguments;

    if (args.size() != 1) {
      std::vector<SourceRange> underlines;
      underlines.reserve(args.size());

      for (const CallArg &arg : args)
        underlines.push_back(arg.value.get()->range);

      this->diag->error(DiagSourceInfo(expr->lparen_loc, std::move(underlines)),
                        "expected exactly one argument for value type construction, have {}", args.size());
      return ExprResult::Error;
    }

    if (args.front().isNamed()) {
      this->diag->error(args.front().name.value().range, "illegal argument name");
      return ExprResult::Error;
    }

    this->typeCheck(args.front().value.get(), ctx);

    if (!args.front().value->isAnnotated() || !this->expectType(args.front().value.get(), type, &expr->callee->range))
      return ExprResult::Error;

    const Type *result = expr->isNew() ? TypeProvider::Pointer(type, true) : type;
    const Constant *constant = (args.front().value->isConstant() && !expr->isNew()) ? args.front().value->getConstant() : nullptr;

    expr->anno = ExprAnno(Typed(result), constant, false);
    expr->call_kind = TypeCall(type, nullptr);
    return ExprResult::Expr;
  } else {
    TypeInfo *info = &this->type_infos.at(type);

    // Every composite type has atleast one constructor, the self make
    assert(!info->make_funcs.empty());

    Function *make_func = this->resolveFunctionCall(expr, &info->make_funcs, ctx, false);

    if (make_func != nullptr) {
      const Type *result = expr->isNew() ? TypeProvider::Pointer(type, true) : type;
      expr->anno = ExprAnno(Typed(result), nullptr, false);
      expr->call_kind = TypeCall(type, make_func);
      return ExprResult::Expr;
    } else {
      return ExprResult::Error;
    }
  }
}

void Sema::typeCheck(IndexAccessExpr *expr, ExprContext &ctx) {
  this->typeCheckNoContext(expr->base.get(), ctx);
  this->typeCheck(expr->index.get(), ctx);

  if (!expr->base->isAnnotated() || !expr->base->isTyped() || !expr->index->isAnnotated())
    return;

  const Type *base_type = expr->base->getType();

  if (base_type->isArrayType() || base_type->isSliceType() || base_type->isStringType() || base_type->isStrType()) {
    if (this->expectType(expr->index.get(), TypeProvider::UInt64(), nullptr))
      expr->anno = ExprAnno(Typed(base_type->indexResult()), nullptr, !base_type->isStrType());
  } else {
    this->diag->error(DiagSourceInfo(expr->lsquare_loc, {expr->base->range}), "expression of type '{}' cannot be indexed", base_type->toString());
  }
}

void Sema::typeCheck(MemberAccessExpr *expr, ExprContext &ctx) {
  std::optional<const Type *> qualifier = this->tryResolveAsTypeName(expr->base.get(), ctx);

  if (!qualifier.has_value())
    return;

  if (qualifier.value() != nullptr)
    return this->typeCheckQualifiedMemberAccess(expr, ctx, qualifier.value());

  this->typeCheckNoContext(expr->base.get(), ctx);

  if (!expr->base->isAnnotated() || !expr->base->isTyped())
    return;

  const Type *base_type = expr->base->getType();

  if (expr->is_arrow != base_type->isPointerType()) {
    return this->diag->error(DiagSourceInfo(expr->dot_loc, {expr->base->range}),
                             "using '{}' for member access through a {}pointer",
                             expr->is_arrow ? "->" : ".", base_type->isPointerType() ? "" : "non-");
  }

  const Type *lookup_type = base_type->isPointerType() ? base_type->toPointerType()->getPointee() : base_type;

  Bindings *members = &this->type_infos.at(lookup_type).members;
  const std::vector<Symbol> *syms = members->lookup(expr->member_name);

  if (syms == nullptr) {
    DiagSourceInfo src_info(expr->dot_loc, {expr->base->range, expr->member_range});
    return this->diag->error(std::move(src_info), "cannot find '{}' in '{}'",
                             expr->member_name, lookup_type->toString());
  }

  const Symbol &first = syms->front();

  switch (first.getTag()) {
  case Symbol::Tag::StructField: {
    expr->anno = ExprAnno(Typed(first.get<StructField *>()->type), nullptr, true);
    expr->field = first.get<StructField *>();
    return;
  }
  case Symbol::Tag::Property: {
    const Constant *constant = (expr->member_name == "length" && lookup_type->isStaticArray()
                                ? ConstantProvider::Integer(lookup_type->toArrayType()->getLength())
                                : nullptr);
    expr->anno = ExprAnno(Typed(first.get<Property *>()->type), constant, false);
    expr->property = first.get<Property *>();
    return;
  }
  case Symbol::Tag::Function:
  case Symbol::Tag::EnumValue: {
    return this->diag->error(expr->dot_loc, "illegal member access");
  }
  case Symbol::Tag::Parameter:
  case Symbol::Tag::Variable:
  case Symbol::Tag::Struct:
  case Symbol::Tag::Variant:
  case Symbol::Tag::Enum:
    assert(false && "symbol cannot be a type member");
  }

  assert(false);
}

void Sema::typeCheckQualifiedMemberAccess(MemberAccessExpr *expr, ExprContext &ctx, const Type *qualifier) {
  if (expr->is_arrow) {
    return this->diag->error(DiagSourceInfo(expr->dot_loc, {expr->base->range}), "using '->' for qualified member access on '{}'", qualifier->toString());
  }

  Bindings *members = &this->type_infos.at(qualifier).members;
  const std::vector<Symbol> *syms = members->lookup(expr->member_name);

  if (syms == nullptr) {
    DiagSourceInfo src_info(expr->dot_loc, {expr->base->range, expr->member_range});
    return this->diag->error(std::move(src_info), "cannot find '{}' in '{}'",
                             expr->member_name, qualifier->toString());
  }

  const Symbol &first = syms->front();

  // Qualified member access (not in a call) can only be used to access enum members (for now)
  switch (first.getTag()) {
  case Symbol::Tag::EnumValue: {
    expr->anno = ExprAnno(Typed(qualifier), ConstantProvider::Enum(first.get<EnumValue *>()), false);
    return;
  }
  case Symbol::Tag::Function: {
    return this->diag->error(expr->dot_loc, "'{}.{}' can only be used in a function call",
                             qualifier->toString(), expr->member_name);
  }
  case Symbol::Tag::StructField:
  case Symbol::Tag::Property:
    return this->diag->error(expr->dot_loc,
                              "illegal qualified member access");
  case Symbol::Tag::Parameter:
  case Symbol::Tag::Variable:
  case Symbol::Tag::Struct:
  case Symbol::Tag::Variant:
  case Symbol::Tag::Enum:
    assert(false && "symbol cannot be a type member");
  }

  assert(false);
}

void Sema::typeCheck(VariantAccessExpr *expr, ExprContext &ctx) {
  this->typeCheckNoContext(expr->base.get(), ctx);

  TypeNameContext type_ctx(ctx.unit, ctx.scope);
  const Type *access_type = this->resolve(expr->type_name.get(), type_ctx);

  if (!expr->base->isAnnotated() || !expr->base->isTyped() || access_type == nullptr)
    return;

  const Type *type = expr->base->getType();

  if (expr->is_arrow != type->isPointerType())
    return this->diag->error(expr->dot_loc, "using '{}' for variant access through a {}pointer",
                             expr->is_arrow ? "->" : ".", type->isPointerType() ? "" : "non-");

  // e->(t) is equivalent to (*e).(t)
  const Type *lookup_type = type->isPointerType() ? type->toPointerType()->getPointee() : type;

  if (!lookup_type->isVariantType()) {
    std::string fmt = "variant access is only allowed for a variant type or a pointer to variant type (have '{}')";
    return this->diag->error(expr->dot_loc, type->toString());
  }

  VariantAlternative *alternative = lookup_type->toVariantType()->lookupAlternative(access_type);

  if (alternative == nullptr)
    return this->diag->error(expr->type_name->range, "type '{}' is not an alternative of '{}'",
                             access_type->toString(), lookup_type->toString());

  expr->anno = ExprAnno(Typed(access_type), nullptr, true);
  expr->alternative = alternative;
}

void Sema::typeCheck(TypeTestExpr *expr, ExprContext &ctx) {
  this->typeCheckNoContext(expr->operand.get(), ctx);

  TypeNameContext type_ctx(ctx.unit, ctx.scope);
  const Type *test_type = this->resolve(expr->type_name.get(), type_ctx);

  if (!expr->operand->isAnnotated() || !expr->operand->isTyped() || test_type == nullptr)
    return;

  const Type *type = expr->operand->getType();

  if (!type->isVariantType())
    return this->diag->error(expr->operand->range, "expression must be a variant, have '{}'", type->toString());

  VariantAlternative *alternative = type->toVariantType()->lookupAlternative(test_type);

  if (alternative == nullptr)
    return this->diag->error(expr->type_name->range, "type '{}' is not an alternative of '{}'",
                             test_type->toString(), type->toString());

  expr->anno = ExprAnno(Typed(TypeProvider::Bool()), nullptr, false);
  expr->alternative = alternative;
}

void Sema::typeCheck(TypeCastExpr *expr, ExprContext &ctx) {
  Expr *operand = expr->operand.get();
  this->typeCheck(operand, ctx);

  TypeNameContext type_ctx(ctx.unit, ctx.scope);
  const Type *target = this->resolve(expr->type_name.get(), type_ctx);

  if (!operand->isAnnotated() || target == nullptr)
    return;

  expr->target = target;

  if (!operand->isTyped())
    return this->diag->error(operand->range, "casted expression in type cast must be typed");

  const Type *type = operand->getType();

  if (!type->isExplicitlyConvertibleTo(target))
    return this->diag->error(operand->range, "cannot cast type '{}' to '{}'", type->toString(), target->toString());

  const Constant *constant = (operand->isConstant() && !target->isVariantType()
                              ? operand->getConstant()->convert(operand->getType(), target)
                              : nullptr);
  expr->anno = ExprAnno(Typed(target), constant, false);
}

void Sema::typeCheck(IdentifierExpr *expr, ExprContext &ctx) {
  const std::vector<Symbol> *syms = this->lookupSymbol(expr->name, ctx.scope, ctx.unit);

  if (syms == nullptr)
    return this->diag->error(expr->range, "use of undeclared identifier '{}'", expr->name);

  const Symbol &first = syms->front();

  switch (first.getTag()) {
  case Symbol::Tag::Variable: {
    Variable *var = first.get<Variable *>();

    if (var->type != nullptr) {
      expr->anno = ExprAnno(Typed(var->type), var->constant, !var->is_constant);
      expr->var = var;
    }

    return;
  }
  case Symbol::Tag::Parameter: {
    Parameter *param = first.get<Parameter *>();

    if (param->type != nullptr) {
      expr->anno = ExprAnno(Typed(param->type), nullptr, true);
      expr->param = param;
    }

    return;
  }
  case Symbol::Tag::Function:
    return this->diag->error(expr->range, "'{}' can only be used in a function call", expr->name);
  case Symbol::Tag::Struct:
  case Symbol::Tag::Variant:
  case Symbol::Tag::Enum:
    return this->diag->error(expr->range, "'{}' is not an expression", expr->name);
  case Symbol::Tag::StructField:
  case Symbol::Tag::EnumValue:
  case Symbol::Tag::Property:
    assert(false && "symbol should not be visible here");
  }

  assert(false);
}

void Sema::typeCheck(ParenExpr *expr, ExprContext &ctx) {
  this->typeCheck(expr->expr.get(), ctx);
  if (expr->expr->isAnnotated())
    expr->anno = expr->expr->anno;
}

void Sema::typeCheck(ListExpr *expr, ExprContext &ctx) {
  this->diag->error(expr->range, "expression list cannot be used in this context");
}

bool Sema::typeCheckListComponents(ListExpr *expr, ExprContext &ctx) {
  for (std::unique_ptr<Expr> &component : expr->components) {
    this->typeCheck(component.get(), ctx);

    if (!component->isAnnotated())
      return false;
  }

  return true;
}

void Sema::typeCheck(CompositeLiteralExpr *expr, ExprContext &ctx) {
  for (std::unique_ptr<Expr> &component : expr->components) {
    this->typeCheck(component.get(), ctx);

    if (!component->isAnnotated())
      return;
  }

  expr->anno = ExprAnno(UntypedCompositeLiteral(), nullptr, false);
}

void Sema::typeCheck(NewExpr *expr, ExprContext &ctx) {
  TypeNameContext type_ctx(ctx.unit, ctx.scope);
  const Type *type = this->resolve(expr->type_name.get(), type_ctx);

  if (type != nullptr)
    this->diag->error(expr->range, "'new' can only be used in a call");
}

void Sema::typeCheck(TypeNameExpr *expr, ExprContext &ctx) {
  TypeNameContext type_ctx(ctx.unit, ctx.scope);
  const Type *type = this->resolve(expr->type_name.get(), type_ctx);

  if (type != nullptr)
    this->diag->error(expr->range, "'{}' is not an expression", type->toString());
}

void Sema::typeCheck(IntLiteralExpr *expr, ExprContext &ctx) {
  expr->anno = ExprAnno(UntypedIntegerConstant(), ConstantProvider::Integer(expr->value), false);
}

void Sema::typeCheck(BoolLiteralExpr *expr, ExprContext &ctx) {
  expr->anno = ExprAnno(Typed(TypeProvider::Bool()), ConstantProvider::Bool(expr->value), false);
}

void Sema::typeCheck(NullExpr *expr, ExprContext &ctx) {
  expr->anno = ExprAnno(UntypedNullConstant(), ConstantProvider::Null(), false);
}

void Sema::typeCheck(StringLiteralExpr *expr, ExprContext &ctx) {
  expr->anno = ExprAnno(UntypedStringConstant(), ConstantProvider::String(expr->value), false);
}

void Sema::typeCheck(CharLiteralExpr *expr, ExprContext &ctx) {
  expr->anno = ExprAnno(Typed(TypeProvider::Char()), ConstantProvider::Char(expr->value), false);
}
