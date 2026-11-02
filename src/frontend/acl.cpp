#include "acl.hpp"

#include <cassert>

#include "type.hpp"
#include "type-provider.hpp"

bool Symbol::isUser() const {
  switch (this->getTag()) {
  case Tag::Function:    return this->get<Function *>()->loc.has_value();
  case Tag::Parameter:   return this->get<Parameter *>()->loc.has_value();
  case Tag::Variable:    return true;
  case Tag::Struct:      return true;
  case Tag::StructField: return true;
  case Tag::Variant:     return true;
  case Tag::Enum:        return true;
  case Tag::EnumValue:   return true;
  case Tag::Property:    return false;
  }

  assert(false);
}

SourceLocation Symbol::getDeclLoc() const {
  switch (this->getTag()) {
  case Tag::Function:    return this->get<Function *>()->loc.value();
  case Tag::Parameter:   return this->get<Parameter *>()->loc.value();
  case Tag::Variable:    return this->get<Variable *>()->loc;
  case Tag::Struct:      return this->get<Struct *>()->loc;
  case Tag::StructField: return this->get<StructField *>()->loc;
  case Tag::Variant:     return this->get<Variant *>()->loc;
  case Tag::Enum:        return this->get<Enum *>()->loc;
  case Tag::EnumValue:   return this->get<EnumValue *>()->loc;
  case Tag::Property:    assert(false);
  }

  assert(false);
}

void Bindings::bind(std::string_view name, Symbol sym) {
  std::vector<Symbol> &syms = this->bindings[std::string(name)];
  syms.push_back(sym);
}

const std::vector<Symbol> *Bindings::lookup(std::string_view name) const {
  std::unordered_map<std::string, std::vector<Symbol>>::const_iterator it = this->bindings.find(std::string(name));

  if (it != this->bindings.end())
    return &it->second;

  return nullptr;
}

void Scope::bind(std::string_view name, Symbol sym) {
  this->bindings.bind(name, sym);
}

const std::vector<Symbol> *Scope::lookupScope(std::string_view name) const {
  return this->bindings.lookup(name);
}

const std::vector<Symbol> *Scope::lookup(std::string_view name) const {
  const std::vector<Symbol> *syms = this->bindings.lookup(name);

  if (syms != nullptr)
    return syms;

  if (this->parent == nullptr)
    return nullptr;

  return this->parent->lookup(name);
}


const Type *Function::attachedTo() const {
  if (this->qualifier.has_value()) {
    assert(this->qualifier.value().type != nullptr);
    return this->qualifier.value().type;
  } else if (this->receiver != nullptr) {
    assert(this->receiver->type != nullptr);
    return (this->receiver->type->isPointerType()
            ? this->receiver->type->toPointerType()->getPointee()
            : this->receiver->type);
  } else {
    return nullptr;
  }
}

bool Function::hasMainSignature() const {
  assert(this->attachedTo() == nullptr);

  // Require main has signature 'function ([*]str) returns (int32)`, relax this later

  const SliceType *slice_str = TypeProvider::Slice(TypeProvider::Str());

  if (this->params.size() != 1 || !this->params.front().type->isEqualTo(slice_str))
    return false;

  const IntegerType *int32 = TypeProvider::Int32();

  if (this->ret_params.size() != 1 || !this->ret_params.front().type->isEqualTo(int32))
    return false;

  return true;
}

bool Function::hasMakeSignature(const Type *type) const {
  assert(this->attachedTo() != nullptr);
  assert(this->attachedTo()->isEqualTo(type));

  if (receiver != nullptr)
    return false;

  if (this->ret_params.size() != 1)
    return false;

  assert(this->ret_params.front().type != nullptr);
  if (!this->ret_params.front().type->isEqualTo(type))
    return false;

  return true;
}

bool Function::hasTakeSignature(const Type *type) const {
  assert(this->attachedTo() != nullptr);
  assert(this->attachedTo()->isEqualTo(type));

  if (this->receiver == nullptr)
    return false;

  assert(this->receiver->type != nullptr);

  if (!this->receiver->type->isPointerType()
      || this->receiver->type->toPointerType()->isOwning()
      || !this->receiver->type->toPointerType()->getPointee()->isEqualTo(type))
    return false;

  if (this->ret_params.size() != 1)
    return false;

  assert(this->ret_params.front().type != nullptr);
  if (!this->ret_params.front().type->isEqualTo(type))
    return false;

  return true;
}

bool Function::hasDropSignature(const Type *type) const {
  assert(this->attachedTo() != nullptr);
  assert(this->attachedTo()->isEqualTo(type));

  if (this->receiver == nullptr)
    return false;

  assert(this->receiver->type != nullptr);

  if (!this->receiver->type->isPointerType()
      || this->receiver->type->toPointerType()->isOwning()
      || !this->receiver->type->toPointerType()->getPointee()->isEqualTo(type))
    return false;

  if (!this->ret_params.empty())
    return false;

  return true;
}

bool Function::hasSameOverloadSignature(const Function *other) const {
  std::vector<const Type *> this_types = this->flattenedParamTypes();
  std::vector<const Type *> other_types = other->flattenedParamTypes();

  if (this_types.size() != other_types.size())
    return false;

  for (size_t i = 0, n = this_types.size(); i < n; ++i) {
    if (!this_types[i]->isEqualTo(other_types[i]))
      return false;
  }

  return true;
}

std::vector<const Type *> Function::flattenedParamTypes() const {
  std::vector<const Type *> types;

  if (this->receiver != nullptr)
    types.push_back(this->receiver->type);

  for (const Parameter &param : this->params)
    types.push_back(param.type);

  return types;
}

std::vector<const Parameter *> Function::getCallParams(bool is_bound) const {
  bool pass_receiver = this->receiver != nullptr && !is_bound;

  std::vector<const Parameter *> params;
  params.reserve(this->params.size() + (pass_receiver ? 1 : 0));

  if (pass_receiver) {
    assert(this->receiver->type != nullptr && "type is not available");
    params.push_back(this->receiver.get());
  }

  for (const Parameter &param : this->params)
    params.push_back(&param);

  return params;
}

std::string Function::signatureString() const {
  std::string sig = "function ";

  if (this->receiver != nullptr) {
    sig += "(";
    assert(this->receiver->type != nullptr);
    sig += this->receiver->type->toString();
    sig += ") ";
  }

  sig += "(";

  for (size_t i = 0, n = this->params.size(); i < n; ++i) {
    assert(this->params[i].type != nullptr);
    sig += this->params[i].type->toString();

    if (i < n - 1)
      sig += ", ";
  }

  sig += ")";

  if (!this->ret_params.empty()) {
    sig += " returns (";

    for (size_t i = 0, n = this->ret_params.size(); i < n; ++i) {
      assert(this->ret_params[i].type != nullptr);
      sig += this->ret_params[i].type->toString();

      if (i < n - 1)
        sig += ", ";
    }

    sig += ")";
  }

  return sig;
}

Function::BuiltinLowering Function::builtinLowering() const {
  switch (this->builtin_kind.value()) {
  case BuiltinKind::SelfMake:
  case BuiltinKind::DynArrMake:
    return BuiltinLowering::Intrinsic;
  case BuiltinKind::Make:
    return BuiltinLowering::Synthesized;
  case BuiltinKind::DynArrTake:
    return BuiltinLowering::Intrinsic;
  case BuiltinKind::Take:
    return BuiltinLowering::Synthesized;
  case BuiltinKind::Drop:
    return BuiltinLowering::Synthesized;
  case BuiltinKind::DynArrPushBack:
  case BuiltinKind::Assert:
  case BuiltinKind::AssertMsg:
  case BuiltinKind::Panic:
  case BuiltinKind::PanicMsg:
    return BuiltinLowering::Intrinsic;
  }

  assert(false);
}

std::string_view TypeState::untypedDesc() const {
  switch (this->getTag()) {
  case Tag::Typed:
    assert(false);
  case Tag::UntypedIntegerConstant:
    return "untyped integer constant";
  case Tag::UntypedStringConstant:
    return "untyped string constant";
  case Tag::UntypedNullConstant:
    return "untyped null constant";
  case Tag::UntypedCompositeLiteral:
    return "untyped composite literal";
  }

  assert(false);
}

Expr *Expr::unparenthesize() {
  if (this->isParenExpr())
    return this->toParenExpr()->expr->unparenthesize();
  else
    return this;
}

const Expr *Expr::unparenthesize() const {
  if (this->isParenExpr())
    return this->toParenExpr()->expr->unparenthesize();
  else
    return this;
}

std::vector<Expr *> Expr::extractComponents() {
  if (this->isListExpr())
    return view(this->toListExpr()->components);
  else
    return {this};
}

std::vector<const Expr *> Expr::extractComponents() const {
  if (this->isListExpr()) {
    std::vector<const Expr *> result;
    result.reserve(this->toListExpr()->components.size());

    for (const auto &ptr : this->toListExpr()->components)
      result.push_back(ptr.get());

    return result;
  } else {
    return {this};
  }
}

bool Expr::referencesVariable(const Variable *var) const {
  const Expr *unparen = this->unparenthesize();

  if (!unparen->isIdentifierExpr())
    return false;

  assert(unparen->toIdentifierExpr()->var != nullptr);
  return true;
}
