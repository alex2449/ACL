#include "llc-generator.hpp"

#include "../frontend/acl.hpp"
#include "../frontend/type.hpp"
#include "../frontend/type-provider.hpp"
#include "../CFG/cfg.hpp"
#include "llc-type-provider.hpp"

llc::Module LLCGenerator::generate() {
  this->registerBuiltinTypes();
  this->declareRuntime();

  // Declare functions
  for (Function *func : *this->global_ctx.funcs) {
    if (!func->builtin_kind.has_value() || func->builtinLowering() != Function::BuiltinLowering::Intrinsic)
      this->declareFunction(func);
  }

  // Declare type deleters
  for (const auto& [type, info] : *this->global_ctx.type_infos) {
    if (type->requiresDrop(*this->global_ctx.type_infos))
      this->declareDeleter(type);
  }

  // Define functions
  for (Function *func : *this->global_ctx.funcs) {
    if (func->body != nullptr || (func->builtin_kind.has_value() && func->builtinLowering() == Function::BuiltinLowering::Synthesized))
      this->defineFunction(func);
  }

  // Define deleters
  for (const auto &[type, info] : *this->global_ctx.type_infos) {
    if (type->requiresDrop(*this->global_ctx.type_infos))
      this->defineDeleter(type);
  }

  return llc::Module(std::move(this->global_ctx.types), std::move(this->global_ctx.llc_funcs), std::move(this->global_ctx.string_constants));
}

void LLCGenerator::registerBuiltinTypes() {
  this->global_ctx.types_map.insert({
    { TypeProvider::Int8(),   llc::TypeProvider::Int8()   },
    { TypeProvider::Int16(),  llc::TypeProvider::Int16()  },
    { TypeProvider::Int32(),  llc::TypeProvider::Int32()  },
    { TypeProvider::Int64(),  llc::TypeProvider::Int64()  },
    { TypeProvider::UInt8(),  llc::TypeProvider::UInt8()  },
    { TypeProvider::UInt16(), llc::TypeProvider::UInt16() },
    { TypeProvider::UInt32(), llc::TypeProvider::UInt32() },
    { TypeProvider::UInt64(), llc::TypeProvider::UInt64() },
    { TypeProvider::Bool(),   llc::TypeProvider::Bool()   },
    { TypeProvider::Char(),   llc::TypeProvider::Char()   }
  });
}

void LLCGenerator::declareRuntime() {
  llc::Function *rt_malloc = new llc::Function("malloc", {
    new llc::Parameter("n", llc::TypeProvider::UInt64())
  }, llc::TypeProvider::Ptr(), true);

  llc::Function *rt_free = new llc::Function("free", {
    new llc::Parameter("p", llc::TypeProvider::Ptr())
  }, nullptr, true);

  llc::Function *rt_memcpy = new llc::Function("memcpy", {
    new llc::Parameter("dest", llc::TypeProvider::Ptr()),
    new llc::Parameter("src", llc::TypeProvider::Ptr()),
    new llc::Parameter("n", llc::TypeProvider::UInt64())
  }, llc::TypeProvider::Ptr(), true);

  llc::Function *rt_str_equal = new llc::Function("rt_str_equal", {
    new llc::Parameter("a", this->toLLCType(TypeProvider::Str())),
    new llc::Parameter("b", this->toLLCType(TypeProvider::Str()))
  }, llc::TypeProvider::Bool(), true);

  llc::Function *rt_str_concat = new llc::Function("rt_str_concat", {
    new llc::Parameter("a", this->toLLCType(TypeProvider::Str())),
    new llc::Parameter("b", this->toLLCType(TypeProvider::Str()))
  }, this->toLLCType(TypeProvider::String()), true);

  llc::Function *rt_dyn_arr_make = new llc::Function("rt_dyn_arr_make", {
    new llc::Parameter("elem_size", llc::TypeProvider::UInt64()),
  }, this->getOrCreateDynArr(), true);

  llc::Function *rt_dyn_arr_take = new llc::Function("rt_dyn_arr_take", {
    new llc::Parameter("arr", llc::TypeProvider::Pointer(this->getOrCreateDynArr()))
  }, this->getOrCreateDynArr(), true);

  llc::Function *rt_dyn_arr_push_back = new llc::Function("rt_dyn_arr_push_back", {
    new llc::Parameter("arr", llc::TypeProvider::Pointer(this->getOrCreateDynArr())),
    new llc::Parameter("elem", llc::TypeProvider::Ptr())
  }, nullptr, true);

  llc::Function *rt_assert = new llc::Function("rt_assert", {
    new llc::Parameter("condition", llc::TypeProvider::Bool()),
    new llc::Parameter("file", this->toLLCType(TypeProvider::Str())),
    new llc::Parameter("line", llc::TypeProvider::UInt32()),
    new llc::Parameter("column", llc::TypeProvider::UInt32())
  }, nullptr, true);

  llc::Function *rt_assert_msg = new llc::Function("rt_assert_msg", {
    new llc::Parameter("condition", llc::TypeProvider::Bool()),
    new llc::Parameter("message", this->toLLCType(TypeProvider::Str())),
    new llc::Parameter("file", this->toLLCType(TypeProvider::Str())),
    new llc::Parameter("line", llc::TypeProvider::UInt32()),
    new llc::Parameter("column", llc::TypeProvider::UInt32())
  }, nullptr, true);

  llc::Function *rt_panic = new llc::Function("rt_panic", {
    new llc::Parameter("file", this->toLLCType(TypeProvider::Str())),
    new llc::Parameter("line", llc::TypeProvider::UInt32()),
    new llc::Parameter("column", llc::TypeProvider::UInt32())
  }, nullptr, true);

  llc::Function *rt_panic_msg = new llc::Function("rt_panic_msg", {
    new llc::Parameter("message", this->toLLCType(TypeProvider::Str())),
    new llc::Parameter("file", this->toLLCType(TypeProvider::Str())),
    new llc::Parameter("line", llc::TypeProvider::UInt32()),
    new llc::Parameter("column", llc::TypeProvider::UInt32())
  }, nullptr, true);

  this->global_ctx.rt_funcs.insert({
    { "rt_malloc",            rt_malloc            },
    { "rt_free",              rt_free              },
    { "rt_memcpy",            rt_memcpy            },
    { "rt_str_equal",         rt_str_equal         },
    { "rt_str_concat",        rt_str_concat        },
    { "rt_dyn_arr_make",      rt_dyn_arr_make      },
    { "rt_dyn_arr_take",      rt_dyn_arr_take      },
    { "rt_dyn_arr_push_back", rt_dyn_arr_push_back },
    { "rt_assert",            rt_assert            },
    { "rt_assert_msg",        rt_assert_msg        },
    { "rt_panic",             rt_panic             },
    { "rt_panic_msg",         rt_panic_msg         }
  });

  for (const auto& [name, func] : this->global_ctx.rt_funcs)
    this->global_ctx.llc_funcs.push_back(func);
}

llc::Type *LLCGenerator::toLLCType(const Type *type) {
  if (type->isPointerType())
    return llc::TypeProvider::Pointer(this->toLLCType(type->toPointerType()->getPointee()));

  auto it = this->global_ctx.types_map.find(type);
  if (it != this->global_ctx.types_map.end())
      return it->second;

  if (type->isDynamicArray()) {
    // All dynamic arrays are mapped to the same type
    this->global_ctx.types_map.insert({type, this->getOrCreateDynArr()});
  } else if (type->isComposite() || type->isEnumType()) {
    this->declareType(type);
  }

  if ((type->isComposite() && !type->isDynamicArray()) || type->isEnumType())
    this->defineType(type);

  return this->global_ctx.types_map.at(type);
}

llc::StructType *LLCGenerator::getOrCreateDynArr() {
  if (this->global_ctx.dyn_arr == nullptr) {
    this->global_ctx.dyn_arr = llc::TypeProvider::Struct("dyn_arr");
    this->global_ctx.dyn_arr->addFields({
      new llc::Field("data",      llc::TypeProvider::Ptr()),
      new llc::Field("length",    llc::TypeProvider::UInt64()),
      new llc::Field("capacity",  llc::TypeProvider::UInt64()),
      new llc::Field("elem_size", llc::TypeProvider::UInt64())
    });

    this->global_ctx.types.push_back(this->global_ctx.dyn_arr);
  }

  return this->global_ctx.dyn_arr;
}

void LLCGenerator::declareType(const Type *type) {
  llc::Type *llc_type;

  switch (type->getTag()) {
  case Type::Tag::EnumType:
    llc_type = llc::TypeProvider::Enum(type->toIdentifier());
    break;
  case Type::Tag::ArrayType:
    assert(!type->toArrayType()->isDynamic());
    // fallthrough
  case Type::Tag::SliceType:
  case Type::Tag::StructType:
  case Type::Tag::StrType:
  case Type::Tag::StringType:
  case Type::Tag::VariantType:
    llc_type = llc::TypeProvider::Struct(type->toIdentifier());
    break;
  default:
    assert(false && "unexpected type in LLCGenerator::declareType");
  }

  this->global_ctx.types.push_back(llc_type);
  this->global_ctx.types_map.insert({type, llc_type});
}

void LLCGenerator::defineType(const Type *type) {
  llc::Type *llc_type = this->global_ctx.types_map.at(type);

  switch (type->getTag()) {
  case Type::Tag::EnumType: {
    const std::vector<EnumValue> &values = type->toEnumType()->getEnum()->values;
    llc_type->toEnumType()->values.reserve(values.size());

    for (const EnumValue &value : values) {
      llc::EnumValue *llc_value = new llc::EnumValue(type->toEnumType()->getEnum()->name + "_" + value.name);
      llc_type->toEnumType()->values.push_back(llc_value);
      this->global_ctx.enum_values_map.insert({&value, llc_value});
    }

    break;
  }
  case Type::Tag::ArrayType: {
    assert(!type->toArrayType()->isDynamic());

    llc::Type *field_type = llc::TypeProvider::Array(this->toLLCType(type->toArrayType()->getElement()),
                                                     type->toArrayType()->getLength());
    llc_type->toStructType()->addField(new llc::Field("buffer", field_type));
    break;
  }
  case Type::Tag::SliceType: {
    llc::Type *llc_element = this->toLLCType(type->toSliceType()->getElement());

    llc_type->toStructType()->addFields({
      new llc::Field("data",   llc::TypeProvider::Pointer(llc_element)),
      new llc::Field("length", llc::TypeProvider::UInt64())
    });

    break;
  }
  case Type::Tag::StructType: {
    const std::vector<StructField> &fields = type->toStructType()->getStruct()->fields;

    if (fields.empty()) {
      llc_type->toStructType()->addField(new llc::Field("dummy", llc::TypeProvider::UInt8()));
      break;
    }

    llc_type->toStructType()->fields.reserve(fields.size());

    for (const StructField &field : fields) {
      llc::Field *llc_field = new llc::Field(field.name, this->toLLCType(field.type));
      llc_type->toStructType()->addField(llc_field);
      this->global_ctx.struct_fields_map.insert({&field, llc_field});
    }

    break;
  }
  case Type::Tag::StrType: {
    llc_type->toStructType()->addFields({
      new llc::Field("data",   llc::TypeProvider::Pointer(llc::TypeProvider::Char())),
      new llc::Field("length", llc::TypeProvider::UInt64())
    });

    break;
  }
  case Type::Tag::StringType: {
    llc_type->toStructType()->addFields({
      new llc::Field("data",     llc::TypeProvider::Pointer(llc::TypeProvider::Char())),
      new llc::Field("length",   llc::TypeProvider::UInt64()),
      new llc::Field("capacity", llc::TypeProvider::UInt64())
    });

    break;
  }
  case Type::Tag::VariantType: {
    llc::EnumType *tag_type = this->declareVariantTag(type->toVariantType()->getVariant());
    llc::UnionType *payload_type = this->declareVariantPayload(type->toVariantType()->getVariant());

    llc_type->toStructType()->addFields({
      new llc::Field("tag",     tag_type),
      new llc::Field("payload", payload_type)
    });

    break;
  }
  default:
    assert(false);
  }
}

llc::EnumType *LLCGenerator::declareVariantTag(const Variant *variant) {
  llc::EnumType *tag = llc::TypeProvider::Enum(variant->name + "_Tag");
  this->global_ctx.types.push_back(tag);

  const std::vector<VariantAlternative> &alts = variant->alternatives;
  tag->values.reserve(alts.size());

  for (const VariantAlternative &alt : alts) {
    llc::EnumValue *value = new llc::EnumValue(variant->name + "_" + alt.type->toIdentifier());
    tag->values.push_back(value);
    this->global_ctx.variant_tags_map.insert({&alt, value});
  }

  return tag;
}

llc::UnionType *LLCGenerator::declareVariantPayload(const Variant *variant) {
  llc::UnionType *payload = llc::TypeProvider::Union(variant->name + "_Payload");
  this->global_ctx.types.push_back(payload);

  const std::vector<VariantAlternative> &alts = variant->alternatives;
  payload->fields.reserve(alts.size());

  for (size_t i = 0, n = alts.size(); i < n; ++i) {
    llc::Field *field = new llc::Field("alt_" + alts[i].type->toIdentifier(), this->toLLCType(alts[i].type));
    payload->addField(field);
    this->global_ctx.variant_payload_fields_map.insert({&alts[i], field});
  }

  return payload;
}

void LLCGenerator::declareFunction(const Function *func) {
  std::string name;

  if (func->is_main) {
    name = "acl_main";
  } else {
    if (func->attachedTo() != nullptr) {
      name += func->attachedTo()->toIdentifier();
      name.push_back('_');
    }

    name += func->name;

    if (func->is_overloaded) {
      if (func->receiver != nullptr)
        name += "_" + func->receiver->type->toIdentifier();

      for (const Parameter &param : func->params) {
        name.push_back('_');
        name += param.type->toIdentifier();
      }
    }
  }

  std::vector<llc::Parameter *> llc_params;
  llc::Type *return_type;

  if (func->receiver != nullptr) {
    llc::Parameter *llc_param = new llc::Parameter(func->receiver->name.value(), this->toLLCType(func->receiver->type));
    llc_params.push_back(llc_param);
  }

  for (const Parameter &param : func->params) {
    // TODO: Fix param name
    llc::Parameter *llc_param = new llc::Parameter(param.isNamed() ? param.getName() : "i", this->toLLCType(param.type));
    llc_params.push_back(llc_param);
  }

  const std::vector<Parameter> &ret_params = func->ret_params;

  if (ret_params.empty()) {
    return_type = nullptr;
  } else {
    return_type = this->toLLCType(ret_params.front().type);

    for (size_t i = 1, n = ret_params.size(); i < n; ++i) {
      std::string param_name = "r" + std::to_string(i);

      llc::Type *param_type = llc::TypeProvider::Pointer(this->toLLCType(ret_params[i].type));
      llc::Parameter *llc_param = new llc::Parameter(std::move(param_name), param_type);
      llc_params.push_back(llc_param);
    }
  }

  llc::Function *llc_func = new llc::Function(std::move(name), std::move(llc_params), return_type, func->is_extern);
  this->global_ctx.llc_funcs.push_back(llc_func);
  this->global_ctx.funcs_map.insert({func, llc_func});
}

void LLCGenerator::declareDeleter(const Type *type) {
  llc::Type *llc_type = this->toLLCType(type);

  std::string name = type->toIdentifier() + "_delete";

  llc::Type *this_type = llc::TypeProvider::Pointer(llc_type);
  llc::Parameter *this_param = new llc::Parameter("this", this_type);

  llc::Function *deleter = new llc::Function(std::move(name), {this_param}, nullptr, false);
  this->global_ctx.llc_funcs.push_back(deleter);
  this->global_ctx.type_deleters_map.insert({type, deleter});
}

void LLCGenerator::defineFunction(const Function *func) {
  if (func->body != nullptr)
    return this->defineUser(func);

  switch (func->builtin_kind.value()) {
  case Function::BuiltinKind::Make:
    return this->defineMake(func);
  case Function::BuiltinKind::Take:
    return this->defineTake(func);
  case Function::BuiltinKind::Drop:
    return this->defineDrop(func);
  default:
    assert(false && "not a synthesized function");
  }
}

void LLCGenerator::defineDeleter(const Type *type) {
  llc::Function *deleter = this->global_ctx.type_deleters_map.at(type);
  llc::Parameter *this_param = deleter->params.at(0);

  FunctionContext ctx(deleter);

  llc::BasicBlock *entry = ctx.newBlock();
  llc::BasicBlock *return_block = ctx.newBlock();
  llc::BasicBlock *cleanup_block = ctx.newBlock();

  deleter->blocks.push_back(entry);
  deleter->blocks.push_back(return_block);
  deleter->blocks.push_back(cleanup_block);

  llc::Value *this_value = this->buildLoad(ctx, entry, new llc::Place(this_param))->asValue();
  llc::Value *null_ = new llc::NullValue(this_value->type);
  llc::Value *cond = this->buildBinaryOp(ctx, entry, llc::TypeProvider::Bool(), llc::BinaryOp::EQ, this_value, null_)->asValue();
  this->buildIf(entry, cond, return_block, cleanup_block);

  this->buildReturn(return_block, nullptr);

  assert(type->requiresDrop(*this->global_ctx.type_infos));
  llc::Function *ir_drop = this->global_ctx.funcs_map.at(this->global_ctx.type_infos->at(type).drop);
  llc::Call drop_call = llc::Call(ir_drop, {this->buildLoad(ctx, cleanup_block, new llc::Place(this_param))->asValue()});
  this->buildVoidCall(cleanup_block, std::move(drop_call));

  llc::Call free_call = llc::Call(this->global_ctx.rt_funcs.at("rt_free"), {this->buildLoad(ctx, cleanup_block, new llc::Place(this_param))->asValue()});
  this->buildVoidCall(cleanup_block, std::move(free_call));

  this->buildReturn(cleanup_block, nullptr);
}

void LLCGenerator::defineUser(const Function *func) {
  llc::Function *llc_func = this->global_ctx.funcs_map.at(func);
  cfg::CFG &cfg = this->global_ctx.cfg_map->at(func);

  FunctionContext ctx(llc_func);

  // Register params
  size_t i = 0;
  if (func->receiver != nullptr)
    ctx.params_map.insert({func->receiver.get(), llc_func->params.at(i++)});

  for (const Parameter &param : func->params)
    ctx.params_map.insert({&param, llc_func->params.at(i++)});

  const std::vector<Parameter> &ret_params = func->ret_params;
  if (ret_params.size() >= 1) {
    for (size_t j = 1, n = ret_params.size(); j < n; ++j)
      ctx.params_map.insert({&ret_params[j], llc_func->params.at(i++)});
  }

  for (cfg::BasicBlock *bb : cfg.blocks) {
    llc::BasicBlock *llc_bb = ctx.newBlock();
    llc_func->blocks.push_back(llc_bb);
    ctx.blocks_map.insert({bb, llc_bb});
  }

  for (cfg::BasicBlock *bb : cfg.blocks) {
    llc::BasicBlock *llc_bb = ctx.blocks_map.at(bb);

    for (cfg::Instruction *inst : bb->insts)
      this->visit(inst, ctx, llc_bb);

    this->visit(bb->term, ctx, llc_bb);
  }
}

void LLCGenerator::defineMake(const Function *func) {
  const Type *type = func->ret_params.front().type;

  switch (type->getTag()) {
  case Type::Tag::StructType:
    return this->defineStructMake(func);
  case Type::Tag::VariantType:
    return this->defineVariantMake(func);
  default:
    assert(false && "unexpected type");
  }
}

void LLCGenerator::defineStructMake(const Function *func) {
  const StructType *type = func->ret_params.front().type->toStructType();
  llc::Type *llc_type = this->toLLCType(type);
  llc::Function *llc_func = this->global_ctx.funcs_map.at(func);

  FunctionContext ctx(llc_func);

  llc::BasicBlock *entry = ctx.newBlock();
  llc_func->blocks.push_back(entry);

  llc::Variable *temp = this->newTemporary(ctx, entry, llc_type);

  const std::vector<StructField> &fields = type->getStruct()->fields;

  for (size_t i = 0, n = fields.size(); i < n; ++i) {
    llc::Field *llc_field = this->global_ctx.struct_fields_map.at(&fields[i]);
    llc::Place *place = new llc::Place(temp, {llc_field});
    llc::Value *value = buildLoad(ctx, entry, new llc::Place(llc_func->params.at(i)))->asValue();

    buildStore(entry, place, value);
  }

  buildReturn(entry, buildLoad(ctx, entry, new llc::Place(temp))->asValue());
}

void LLCGenerator::defineVariantMake(const Function *func) {
  const VariantType *type = func->ret_params.front().type->toVariantType();
  const VariantAlternative *alt = type->lookupAlternative(func->params.front().type);
  assert(alt != nullptr);
  llc::Type *llc_type = this->toLLCType(type);
  llc::Function *llc_func = this->global_ctx.funcs_map.at(func);

  FunctionContext ctx(llc_func);

  llc::BasicBlock *entry = ctx.newBlock();
  llc_func->blocks.push_back(entry);

  llc::Variable *temp = this->newTemporary(ctx, entry, llc_type);

  llc::Place *tag_place = new llc::Place(temp, {temp->type->toStructType()->fields.at(0)});
  llc::Value *tag_value = new llc::EnumConstantValue(tag_place->getType(), this->global_ctx.variant_tags_map.at(alt));
  this->buildStore(entry, tag_place, tag_value);

  llc::Place *alt_place = new llc::Place(temp, {temp->type->toStructType()->fields.at(1), this->global_ctx.variant_payload_fields_map.at(alt)});
  llc::Value *alt_value = this->buildLoad(ctx, entry, new llc::Place(llc_func->params.at(0)))->asValue();
  this->buildStore(entry, alt_place, alt_value);

  this->buildReturn(entry, this->buildLoad(ctx, entry, new llc::Place(temp))->asValue());
}

void LLCGenerator::defineTake(const Function *func) {
  const Type *type = func->ret_params.front().type;

  switch (type->getTag()) {
  case Type::Tag::ArrayType:
    return this->defineArrayTake(func);
  case Type::Tag::StructType:
    return this->defineStructTake(func);
  case Type::Tag::VariantType:
    return this->defineVariantTake(func);
  default:
    assert(false && "unexpected type");
  }
}

void LLCGenerator::defineArrayTake(const Function *func) {
  const ArrayType *type = func->ret_params.front().type->toArrayType();

  llc::Function *llc_func = this->global_ctx.funcs_map.at(func);
  llc::Type *return_type = llc_func->return_type;

  FunctionContext ctx(llc_func);

  llc::BasicBlock *entry = ctx.newBlock();
  llc_func->blocks.push_back(entry);

  llc::Variable *temp = this->newTemporary(ctx, entry, return_type);

  assert(!type->isDynamic());
  const Type *element = type->getElement();
  uint64_t length = type->toArrayType()->getLength();

  assert(element->requiresTake(*this->global_ctx.type_infos));

  llc::Type *llc_type = this->toLLCType(type);
  llc::Parameter *this_ = llc_func->params.at(0);

  llc::BasicBlock *loop_cond = ctx.newBlock();
  llc::BasicBlock *loop_body = ctx.newBlock();
  llc::BasicBlock *loop_end = ctx.newBlock();

  llc_func->blocks.push_back(loop_cond);
  llc_func->blocks.push_back(loop_body);
  llc_func->blocks.push_back(loop_end);

  llc::Variable *i = this->newTemporary(ctx, entry, llc::TypeProvider::UInt64());
  buildStore(entry, new llc::Place(i), U64(0));
  buildGoto(entry, loop_cond);

  llc::Value *left = buildLoad(ctx, loop_cond, new llc::Place(i))->asValue();
  llc::Value *cond = buildBinaryOp(ctx, loop_cond, llc::TypeProvider::Bool(), llc::BinaryOp::LT, left, U64(length))->asValue();
  buildIf(loop_cond, cond, loop_body, loop_end);

  llc::Value *left2 = buildLoad(ctx, loop_body, new llc::Place(i))->asValue();
  llc::Value *dec_i= buildBinaryOp(ctx, loop_body, llc::TypeProvider::UInt64(), llc::BinaryOp::Add, left2, U64(1))->asValue();
  buildStore(loop_body, new llc::Place(i), dec_i);
  llc::Value *load_this = buildLoad(ctx, loop_body, new llc::Place(this_))->asValue();
  llc::Field *buffer_field = llc_type->toStructType()->fields.at(0);

  llc::Place *src_place = new llc::Place(llc::PlaceBase(llc::Deref(load_this, nullptr)), {buffer_field, buildLoad(ctx, loop_body, new llc::Place(i))->asValue()});
  llc::Value *taken = this->take(ctx, element, src_place, loop_body)->asValue();
  llc::Place *dst_place = new llc::Place(temp, {buffer_field, buildLoad(ctx, loop_body, new llc::Place(i))->asValue()});
  buildStore(loop_body, dst_place, taken);
  buildGoto(loop_body, loop_cond);

  buildReturn(loop_end, buildLoad(ctx, loop_end, new llc::Place(temp))->asValue());
}

void LLCGenerator::defineStructTake(const Function *func) {
  const StructType *type = func->ret_params.front().type->toStructType();

  llc::Function *llc_func = this->global_ctx.funcs_map.at(func);
  llc::Type *return_type = llc_func->return_type;

  FunctionContext ctx(llc_func);

  llc::BasicBlock *entry = ctx.newBlock();
  llc_func->blocks.push_back(entry);

  llc::Variable *temp = this->newTemporary(ctx, entry, return_type);

  const std::vector<StructField> &fields = type->getStruct()->fields;

  for (size_t i = 0, n = fields.size(); i < n; ++i) {
    llc::Field *llc_field = this->global_ctx.struct_fields_map.at(&fields[i]);
    llc::Value *this_value = buildLoad(ctx, entry, new llc::Place(llc_func->params.at(0)))->asValue();
    llc::Place *src_place = new llc::Place(llc::PlaceBase(llc::Deref(this_value, nullptr)), {llc_field});

    llc::Value *taken = this->take(ctx, fields[i].type, src_place, entry)->asValue();
    llc::Place *target_place = new llc::Place(temp, {llc_field});
    buildStore(entry, target_place, taken);
  }

  buildReturn(entry, buildLoad(ctx, entry, new llc::Place(temp))->asValue());
}

void LLCGenerator::defineVariantTake(const Function *func) {
  const VariantType *type = func->ret_params.front().type->toVariantType();

  llc::Function *llc_func = this->global_ctx.funcs_map.at(func);
  llc::Type *return_type = llc_func->return_type;

  FunctionContext ctx(llc_func);

  llc::Type *llc_type = this->toLLCType(type);
  llc::Parameter *this_ = llc_func->params.front();

  llc::BasicBlock *entry = ctx.newBlock();
  llc_func->blocks.push_back(entry);

  llc::Variable *temp = this->newTemporary(ctx, entry, return_type);

  llc::Field *tag_field = llc_type->toStructType()->fields.at(0);
  llc::Place *tag_src_place = new llc::Place(llc::PlaceBase(llc::Deref(this->buildLoad(ctx, entry, new llc::Place(this_))->asValue(), nullptr)), {tag_field});
  llc::Value *tag_src_value = buildLoad(ctx, entry, tag_src_place)->asValue();
  llc::Place *tag_dest_place = new llc::Place(temp, {tag_field});
  this->buildStore(entry, tag_dest_place, tag_src_value);

  llc::Place *tag_src_place2 = new llc::Place(llc::PlaceBase(llc::Deref(buildLoad(ctx, entry, new llc::Place(this_))->asValue(), nullptr)), {tag_field});
  llc::Value *cond = this->buildLoad(ctx, entry, tag_src_place2)->asValue();

  std::vector<llc::SwitchClause> clauses;

  llc::BasicBlock *merge = ctx.newBlock();

  for (const VariantAlternative &alt : type->toVariantType()->getVariant()->alternatives) {
    llc::EnumConstantValue *label = new llc::EnumConstantValue(llc_type->toStructType()->fields.front()->type, this->global_ctx.variant_tags_map.at(&alt));
    llc::BasicBlock *alt_block = ctx.newBlock();

    assert(alt.type != nullptr);
    // Take from (*%0).payload.alt, where %0 = Load(this)
    llc::Value *load_this = buildLoad(ctx, alt_block, new llc::Place(this_))->asValue();
    llc::Place *src = new llc::Place(llc::PlaceBase(llc::Deref(load_this, nullptr)), {llc_type->toStructType()->fields.at(1), this->global_ctx.variant_payload_fields_map.at(&alt)});

    llc::Value *taken = this->take(ctx, alt.type, src, alt_block)->asValue();

    // Store the value in temp.payload.alt
    llc::Place *target = new llc::Place(temp, {llc_type->toStructType()->fields.at(1), this->global_ctx.variant_payload_fields_map.at(&alt)});
    buildStore(alt_block, target, taken);
    buildGoto(alt_block, merge);

    llc_func->blocks.push_back(alt_block);
    clauses.push_back(llc::SwitchClause(label, alt_block));
  }

  llc::BasicBlock *default_block = ctx.newBlock();
  llc_func->blocks.push_back(default_block);
  this->panic(ctx, default_block, type->getVariant()->loc, "exhaustive switch reached default clause");

  buildSwitch(entry, cond, std::move(clauses), default_block);

  llc_func->blocks.push_back(merge);
  buildReturn(merge, buildLoad(ctx, merge, new llc::Place(temp))->asValue());
}

llc::Variable *LLCGenerator::take(FunctionContext &ctx, const Type *type, llc::Place *place, llc::BasicBlock *insert) {
  if (!type->requiresTake(*this->global_ctx.type_infos))
    return buildLoad(ctx, insert, place);

  if (type->isPointerType()) {
    llc::Variable *read_ptr = buildLoad(ctx, insert, place);
    buildStore(insert, place, new llc::NullValue(place->getType()));
    return read_ptr;
  } else {
    llc::Value *addr = buildAddressOf(ctx, insert, place)->asValue();
    llc::Function *take_func = (type->isDynamicArray()
                                ? this->global_ctx.rt_funcs.at("rt_dyn_arr_take")
                                : this->global_ctx.funcs_map.at(this->global_ctx.type_infos->at(type).take));
    llc::Call take_call = llc::Call(take_func, {addr});
    return buildCall(ctx, insert, std::move(take_call));
  }
}

void LLCGenerator::defineDrop(const Function *func) {
  const Type *type = func->receiver->type->toPointerType()->getPointee();

  switch (type->getTag()) {
  case Type::Tag::ArrayType:
    return this->defineArrayDrop(func);
  case Type::Tag::StructType:
    return this->defineStructDrop(func);
  case Type::Tag::VariantType:
    return this->defineVariantDrop(func);
  default:
    assert(false && "unexpected type");
  }
}

void LLCGenerator::defineArrayDrop(const Function *func) {
  llc::Function *llc_func = this->global_ctx.funcs_map.at(func);
  FunctionContext ctx(llc_func);
  const ArrayType *type = func->receiver->type->toPointerType()->getPointee()->toArrayType();

  llc::BasicBlock *entry = ctx.newBlock();
  llc_func->blocks.push_back(entry);

  llc::Type *llc_type = this->toLLCType(type);
  llc::Parameter *this_ = llc_func->params.front();

  const Type *element = type->getElement();
  llc::PointerType *llc_element_ptr = llc::TypeProvider::Pointer(this->toLLCType(element));

  if (type->isDynamic()) {
    llc::Field *data_field = llc_type->toStructType()->fields.at(0);

    if (element->requiresDrop(*this->global_ctx.type_infos)) {
      llc::Field *length_field = llc_type->toStructType()->fields.at(1);

      llc::BasicBlock *loop_cond = ctx.newBlock();
      llc::BasicBlock *loop_body = ctx.newBlock();
      llc::BasicBlock *loop_end = ctx.newBlock();

      llc_func->blocks.push_back(loop_cond);
      llc_func->blocks.push_back(loop_body);
      llc_func->blocks.push_back(loop_end);

      llc::Variable *counter = this->newTemporary(ctx, entry, llc::TypeProvider::UInt64());
      llc::Value *load_this = buildLoad(ctx, entry, new llc::Place(this_))->asValue();
      llc::Value *load_length = buildLoad(ctx, entry, new llc::Place(llc::PlaceBase(llc::Deref(load_this, nullptr)), {length_field}))->asValue();
      buildStore(entry, new llc::Place(counter), load_length);
      buildGoto(entry, loop_cond);

      llc::Value *load_counter = buildLoad(ctx, loop_cond, new llc::Place(counter))->asValue();
      llc::Value *cond = buildBinaryOp(ctx, loop_cond, llc::TypeProvider::Bool(), llc::BinaryOp::EQ, load_counter, U64(0))->asValue();
      buildIf(loop_cond, cond, loop_end, loop_body);

      llc::Value *load_counter2 = buildLoad(ctx, loop_body, new llc::Place(counter))->asValue();
      llc::Value *dec = buildBinaryOp(ctx, loop_body, llc::TypeProvider::UInt64(), llc::BinaryOp::Sub, load_counter2, U64(1))->asValue();
      buildStore(loop_body, new llc::Place(counter), dec);
      llc::Value *load_this2 = buildLoad(ctx, loop_body, new llc::Place(this_))->asValue();
      llc::Value *load_data = buildLoad(ctx, loop_body, new llc::Place(llc::PlaceBase(llc::Deref(load_this2, nullptr)), {data_field}))->asValue();
      llc::Value *cast_data = buildCast(ctx, loop_body, load_data, llc_element_ptr)->asValue();
      llc::Value *load_counter3 = buildLoad(ctx, loop_body, new llc::Place(counter))->asValue();
      this->drop(ctx, element, new llc::Place(llc::PlaceBase(llc::Deref(cast_data, load_counter3))), loop_body);
      buildGoto(loop_body, loop_cond);
    }

    llc::BasicBlock *insert = llc_func->blocks.back();
    llc::Value *load_this = this->buildLoad(ctx, insert, new llc::Place(this_))->asValue();
    llc::Value *load_data = this->buildLoad(ctx, insert, new llc::Place(llc::PlaceBase(llc::Deref(load_this, nullptr)), {data_field}))->asValue();
    llc::Call free_call = llc::Call(this->global_ctx.rt_funcs.at("rt_free"), {load_data});
    this->buildVoidCall(insert, free_call);
    this->buildReturn(insert, nullptr);
  } else {
    llc::BasicBlock *loop_cond = ctx.newBlock();
    llc::BasicBlock *loop_body = ctx.newBlock();
    llc::BasicBlock *loop_end = ctx.newBlock();

    llc_func->blocks.push_back(loop_cond);
    llc_func->blocks.push_back(loop_body);
    llc_func->blocks.push_back(loop_end);

    llc::Variable *counter = this->newTemporary(ctx, entry, llc::TypeProvider::UInt64());
    buildStore(entry, new llc::Place(counter), U64(type->getLength()));
    buildGoto(entry, loop_cond);

    llc::Value *left = buildLoad(ctx, loop_cond, new llc::Place(counter))->asValue();
    llc::Value *cond = buildBinaryOp(ctx, loop_cond, llc::TypeProvider::Bool(), llc::BinaryOp::EQ, left, U64(0))->asValue();
    buildIf(loop_cond, cond, loop_end, loop_body);

    llc::Value *left2 = buildLoad(ctx, loop_body, new llc::Place(counter))->asValue();
    llc::Value *dec_counter = buildBinaryOp(ctx, loop_body, llc::TypeProvider::UInt64(), llc::BinaryOp::Sub, left2, U64(1))->asValue();
    buildStore(loop_body, new llc::Place(counter), dec_counter);
    llc::Value *load_this = buildLoad(ctx, entry, new llc::Place(this_))->asValue();
    llc::Field *buffer_field = llc_type->toStructType()->fields.at(0);
    llc::Value *counter_value = buildLoad(ctx, loop_body, new llc::Place(counter))->asValue();
    this->drop(ctx, element, new llc::Place(llc::PlaceBase(llc::Deref(load_this, nullptr)), {buffer_field, counter_value}), loop_body);
    buildGoto(loop_body, loop_cond);

    buildReturn(loop_end, nullptr);
  }
}

void LLCGenerator::defineStructDrop(const Function *func) {
  llc::Function *llc_func = this->global_ctx.funcs_map.at(func);
  FunctionContext ctx(llc_func);
  const StructType *type = func->receiver->type->toPointerType()->getPointee()->toStructType();

  llc::BasicBlock *entry = ctx.newBlock();
  llc_func->blocks.push_back(entry);

  llc::Parameter *this_ = llc_func->params.front();

  for (const StructField &field : type->toStructType()->getStruct()->fields) {
    const Type *field_type = field.type;
    assert(field_type != nullptr);

    if (field_type->requiresDrop(*this->global_ctx.type_infos)) {
      llc::RegisterValue *this_value = buildLoad(ctx, entry, new llc::Place(this_))->asValue();

      llc::Field *llc_field = this->global_ctx.struct_fields_map.at(&field);
      llc::Place *place = new llc::Place(llc::PlaceBase(llc::Deref(this_value, nullptr)), {llc_field});

      this->drop(ctx, field_type, place, entry);
    }
  }

  this->buildReturn(entry, nullptr);
}

void LLCGenerator::defineVariantDrop(const Function *func) {
  llc::Function *llc_func = this->global_ctx.funcs_map.at(func);
  FunctionContext ctx(llc_func);
  const VariantType *type = func->receiver->type->toPointerType()->getPointee()->toVariantType();

  llc::BasicBlock *entry = ctx.newBlock();
  llc_func->blocks.push_back(entry);

  llc::Type *llc_type = this->toLLCType(type);
  llc::Parameter *this_ = llc_func->params.front();

  llc::Value *this_value = buildLoad(ctx, entry, new llc::Place(this_))->asValue();
  llc::Field *tag_field = llc_type->toStructType()->fields.at(0);
  llc::Value *cond = buildLoad(ctx, entry, new llc::Place(llc::PlaceBase(llc::Deref(this_value, nullptr)), {tag_field}))->asValue();

  std::vector<llc::SwitchClause> clauses;
  clauses.reserve(type->getVariant()->alternatives.size());

  for (const VariantAlternative &alt : type->getVariant()->alternatives) {
    const Type *alt_type = alt.type;
    assert(alt_type != nullptr);

    llc::EnumConstantValue *label = new llc::EnumConstantValue(llc_type->toStructType()->fields.at(0)->type, this->global_ctx.variant_tags_map.at(&alt));

    llc::BasicBlock *alt_cleanup = ctx.newBlock();
    llc_func->blocks.push_back(alt_cleanup);

    if (alt_type->requiresDrop(*this->global_ctx.type_infos)) {
      // %0 = Load(this)
      llc::Value *this_value = buildLoad(ctx, alt_cleanup, new llc::Place(this_))->asValue();

      // cleanup((*%0).payload.f)
      llc::Field *payload_field = llc_type->toStructType()->fields.at(1);
      llc::Field *alt_field = this->global_ctx.variant_payload_fields_map.at(&alt);
      this->drop(ctx, alt_type, new llc::Place(llc::PlaceBase(llc::Deref(this_value, nullptr)), {payload_field, alt_field}), alt_cleanup);
    }

    buildReturn(alt_cleanup, nullptr);
    clauses.push_back(llc::SwitchClause(label, alt_cleanup));
  }

  llc::BasicBlock *default_block = ctx.newBlock();
  llc_func->blocks.push_back(default_block);

  this->panic(ctx, default_block, type->getVariant()->loc, "exhaustive switch reached default clause");
  buildSwitch(entry, cond, std::move(clauses), default_block);
}

void LLCGenerator::drop(FunctionContext &ctx, const Type *type, llc::Place *place, llc::BasicBlock *insert) {
  assert(type->requiresDrop(*this->global_ctx.type_infos));

  if (type->isPointerType()) {
    llc::Value *loaded = buildLoad(ctx, insert, place)->asValue();
    this->drop(ctx, type, loaded, insert);
  } else {
    TypeInfo &type_info = this->global_ctx.type_infos->at(type);
    llc::Function *llc_drop = this->global_ctx.funcs_map.at(type_info.drop);

    llc::Type *ptr_type = llc::TypeProvider::Pointer(this->toLLCType(type));
    llc::Value *take_addr = this->build(insert, new llc::AddressOfInst(ctx.newRegister(ptr_type), place))->asValue();

    llc::Call drop_call = llc::Call(llc_drop, {take_addr});
    this->buildVoidCall(insert, std::move(drop_call));
  }
}

void LLCGenerator::drop(FunctionContext &ctx, const Type *type, llc::Value *value, llc::BasicBlock *insert) {
  const Type *pointee = type->toPointerType()->getPointee();

  if (pointee->requiresDrop(*this->global_ctx.type_infos) && pointee->isComposite()) {
    llc::Function *deleter = this->global_ctx.type_deleters_map.at(pointee);
    llc::Call delete_call = llc::Call(deleter, {value});
    return buildVoidCall(insert, std::move(delete_call));
  }

  if (pointee->requiresDrop(*this->global_ctx.type_infos)) {
    // Have something like ^^T, recursively cleanup pointee
    assert(pointee->isPointerType());

    llc::Place *place = new llc::Place(llc::PlaceBase(llc::Deref(value, nullptr)));
    this->drop(ctx, pointee, place, insert);
  }

  llc::Call free_call = llc::Call(this->global_ctx.rt_funcs.at("rt_free"), {value});
  buildVoidCall(insert, std::move(free_call));
}

void LLCGenerator::visit(const cfg::Instruction *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  switch (inst->tag) {
  case cfg::Instruction::Tag::VoidCallInst:         return this->visit(inst->toVoidCallInst(), ctx, insert);
  case cfg::Instruction::Tag::BeginStorageInst:     return this->visit(inst->toBeginStorageInst(), ctx, insert);
  case cfg::Instruction::Tag::EndStorageInst:       return this->visit(inst->toEndStorageInst(), ctx, insert);
  case cfg::Instruction::Tag::DropValInst:          return this->visit(inst->toDropValInst(), ctx, insert);
  case cfg::Instruction::Tag::InitPlaceInst:        return this->visit(inst->toInitPlaceInst(), ctx, insert);
  case cfg::Instruction::Tag::AssignPlaceInst:      return this->visit(inst->toAssignPlaceInst(), ctx, insert);
  case cfg::Instruction::Tag::UnaryOpInst:          return this->visit(inst->toUnaryOpInst(), ctx, insert);
  case cfg::Instruction::Tag::AddressOfInst:        return this->visit(inst->toAddressOfInst(), ctx, insert);
  case cfg::Instruction::Tag::LoadInst:             return this->visit(inst->toLoadInst(), ctx, insert);
  case cfg::Instruction::Tag::TakeInst:             return this->visit(inst->toTakeInst(), ctx, insert);
  case cfg::Instruction::Tag::CallInst:             return this->visit(inst->toCallInst(), ctx, insert);
  case cfg::Instruction::Tag::BinaryOpInst:         return this->visit(inst->toBinaryOpInst(), ctx, insert);
  case cfg::Instruction::Tag::CompositeLiteralInst: return this->visit(inst->toCompositeLiteralInst(), ctx, insert);
  case cfg::Instruction::Tag::NewInst:              return this->visit(inst->toNewInst(), ctx, insert);
  case cfg::Instruction::Tag::PropertyAccessInst:   return this->visit(inst->toPropertyAccessInst(), ctx, insert);
  case cfg::Instruction::Tag::TypeTestInst:         return this->visit(inst->toTypeTestInst(), ctx, insert);
  case cfg::Instruction::Tag::ValueCastInst:        return this->visit(inst->toValueCastInst(), ctx, insert);
  case cfg::Instruction::Tag::PlaceCastInst:        return this->visit(inst->toPlaceCastInst(), ctx, insert);
  }

  assert(false);
}

void LLCGenerator::visit(const cfg::VoidCallInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Call llc_call = this->lowerCall(inst->call, ctx, insert);
  this->buildVoidCall(insert, std::move(llc_call));
}

void LLCGenerator::visit(const cfg::BeginStorageInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  switch (inst->storage.getTag()) {
  case cfg::Storage::Tag::Variable: {
    Variable *var = inst->storage.get<Variable *>();
    llc::Variable *llc_var = ctx.newVariable(var->name, this->toLLCType(var->type));
    this->build(insert, new llc::VarDeclInst(llc_var));
    ctx.vars_map.insert({var, llc_var});
    return;
  }
  case cfg::Storage::Tag::Temporary: {
    cfg::Temporary *temp = inst->storage.get<cfg::Temporary *>();
    llc::Variable *llc_temp = this->newTemporary(ctx, insert, this->toLLCType(temp->type));
    ctx.temps_map.insert({temp, llc_temp});
    return;
  }
  case cfg::Storage::Tag::Parameter:
    assert(false && "parameters have no BeginStorageInst");
  }

  assert(false);
}

void LLCGenerator::visit(const cfg::EndStorageInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  if (inst->storage.getType()->requiresDrop(*this->global_ctx.type_infos)) {
    llc::Place *place = this->storageToPlace(&inst->storage, ctx);
    this->drop(ctx, inst->storage.getType(), place, insert);
  }
}

llc::Place *LLCGenerator::storageToPlace(const cfg::Storage *storage, FunctionContext &ctx) {
  switch (storage->getTag()) {
  case cfg::Storage::Tag::Variable:
    return new llc::Place(ctx.vars_map.at(storage->get<Variable *>()));
  case cfg::Storage::Tag::Parameter:
    return new llc::Place(ctx.params_map.at(storage->get<Parameter *>()));
  case cfg::Storage::Tag::Temporary:
    return new llc::Place(ctx.temps_map.at(storage->get<cfg::Temporary *>()));
  }

  assert(false);
}

void LLCGenerator::visit(const cfg::DropValInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::RegisterValue *value = new llc::RegisterValue(this->toLLCType(inst->reg->type), ctx.regs_map.at(inst->reg));
  this->drop(ctx, inst->reg->type, value, insert);
}

void LLCGenerator::visit(const cfg::InitPlaceInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Value *llc_value = this->visit(inst->value, ctx, insert);
  llc::Place *llc_place = this->visit(inst->place, ctx, insert);
  this->buildStore(insert, llc_place, llc_value);
}

void LLCGenerator::visit(const cfg::AssignPlaceInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Value *llc_value = this->visit(inst->value, ctx, insert);
  llc::Place *llc_place = this->visit(inst->place, ctx, insert);

  if (inst->place->getType()->requiresDrop(*this->global_ctx.type_infos)) {
    // TODO: Clone llc_place
    this->drop(ctx, inst->place->getType(), llc_place, insert);
    this->buildStore(insert, llc_place, llc_value);
  } else {
    this->buildStore(insert, llc_place, llc_value);
  }
}

void LLCGenerator::visit(const cfg::UnaryOpInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Type *llc_type = this->toLLCType(inst->reg->type);
  llc::UnaryOp llc_op = this->unary_ops_map.at(inst->op);
  llc::Value *llc_operand = this->visit(inst->operand, ctx, insert);
  ctx.mapRegister(inst->reg, this->buildUnaryOp(ctx, insert, llc_type, llc_op, llc_operand));
}

void LLCGenerator::visit(const cfg::AddressOfInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Place *llc_place = this->visit(inst->place, ctx, insert);
  ctx.mapRegister(inst->reg, this->buildAddressOf(ctx, insert, llc_place));
}

void LLCGenerator::visit(const cfg::LoadInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Place *llc_place = this->visit(inst->place, ctx, insert);
  ctx.mapRegister(inst->reg, this->buildLoad(ctx, insert, llc_place));
}

void LLCGenerator::visit(const cfg::TakeInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Place *llc_place = this->visit(inst->place, ctx, insert);
  ctx.mapRegister(inst->reg, this->take(ctx, inst->place->getType(), llc_place, insert));
}

void LLCGenerator::visit(const cfg::BinaryOpInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Value *llc_left = this->visit(inst->left, ctx, insert);
  llc::Value *llc_right = this->visit(inst->right, ctx, insert);

  if (inst->op == cfg::BinaryOp::Add
      && inst->left->getType()->isEqualTo(TypeProvider::Str())
      && inst->right->getType()->isEqualTo(TypeProvider::Str())) {
    llc::Call str_concat_call = llc::Call(this->global_ctx.rt_funcs.at("rt_str_concat"), {llc_left, llc_right});
    ctx.mapRegister(inst->reg, this->buildCall(ctx, insert, std::move(str_concat_call)));
  } else if (inst->op == cfg::BinaryOp::EQ
      && inst->left->getType()->isEqualTo(TypeProvider::Str())
      && inst->right->getType()->isEqualTo(TypeProvider::Str())) {
    llc::Call str_equal_call = llc::Call(this->global_ctx.rt_funcs.at("rt_str_equal"), {llc_left, llc_right});
    ctx.mapRegister(inst->reg, this->buildCall(ctx, insert, std::move(str_equal_call)));
  } else {
    llc::Type *llc_type = this->toLLCType(inst->reg->type);
    llc::BinaryOp llc_op = this->binary_ops_map.at(inst->op);
    ctx.mapRegister(inst->reg, this->buildBinaryOp(ctx, insert, llc_type, llc_op, llc_left, llc_right));
  }
}

void LLCGenerator::visit(const cfg::CallInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Call llc_call = this->lowerCall(inst->call, ctx, insert);
  ctx.mapRegister(inst->reg, this->buildCall(ctx, insert, std::move(llc_call)));
}

llc::Call LLCGenerator::lowerCall(const cfg::Call &call, FunctionContext &ctx, llc::BasicBlock *insert) {
  if (call.func->builtin_kind.has_value() && call.func->builtinLowering() == Function::BuiltinLowering::Intrinsic)
    return this->lowerIntrinsicCall(call, ctx, insert);

  llc::Function *llc_func = this->global_ctx.funcs_map.at(call.func);

  std::vector<llc::Value *> llc_args;
  llc_args.reserve(call.args.size() + call.out_args.size());

  for (const cfg::Value *arg : call.args)
    llc_args.push_back(this->visit(arg, ctx, insert));

  for (const cfg::Place *out_arg : call.out_args) {
    llc::Place *place = this->visit(out_arg, ctx, insert);
    llc_args.push_back(this->buildAddressOf(ctx, insert, place)->asValue());
  }

  return llc::Call(llc_func, std::move(llc_args));
}

llc::Call LLCGenerator::lowerIntrinsicCall(const cfg::Call &call, FunctionContext &ctx, llc::BasicBlock *insert) {
  switch (call.func->builtin_kind.value()) {
  case Function::BuiltinKind::SelfMake:
    assert(false && "should not survive past the CFG");
  case Function::BuiltinKind::DynArrMake: {
    // Pass the element size
    llc::Type *operand = this->toLLCType(call.func->ret_params.at(0).type->toArrayType()->getElement());
    llc::SizeofValue *elem_size = new llc::SizeofValue(llc::TypeProvider::UInt64(), operand);

    return llc::Call(this->global_ctx.rt_funcs.at("rt_dyn_arr_make"), {elem_size});
  }
  case Function::BuiltinKind::DynArrTake: {
    llc::Value *arr = this->visit(call.args.front(), ctx, insert);
    return llc::Call(this->global_ctx.rt_funcs.at("rt_dyn_arr_take"), {arr});
  }
  case Function::BuiltinKind::DynArrPushBack: {
    llc::Value *arr = this->visit(call.args.front(), ctx, insert);
    llc::Value *elem = this->visit(call.args.at(1), ctx, insert);

    llc::Variable *temp = this->newTemporary(ctx, insert, elem->type);
    this->buildStore(insert, new llc::Place(temp), elem);
    llc::RegisterValue *addr = this->buildAddressOf(ctx, insert, new llc::Place(temp))->asValue();
    return llc::Call(this->global_ctx.rt_funcs.at("rt_dyn_arr_push_back"), {arr, addr});
  }
  case Function::BuiltinKind::Assert:
  case Function::BuiltinKind::AssertMsg: {
    llc::Function *func = (call.func->builtin_kind.value() == Function::BuiltinKind::Assert
                           ? this->global_ctx.rt_funcs.at("rt_assert")
                           : this->global_ctx.rt_funcs.at("rt_assert_msg"));

    std::vector<llc::Value *> args;
    args.push_back(this->visit(call.args.at(0), ctx, insert));

    if (call.func->builtin_kind.value() == Function::BuiltinKind::AssertMsg)
      args.push_back(this->visit(call.args.at(1), ctx, insert));

    // const std::string &filename = call.loc.getSource()->getFilename();
    // auto [line, column] = call.loc.convertToLineAndColumn();
    std::string filename = "123";
    mpz_class line = 1;
    mpz_class column = 1;

    args.push_back(Str(ctx, insert, filename));
    args.push_back(U32(line));
    args.push_back(U32(column));

    return llc::Call(func, std::move(args));
  }
  case Function::BuiltinKind::Panic:
  case Function::BuiltinKind::PanicMsg: {
    llc::Function *func = (call.func->builtin_kind.value() == Function::BuiltinKind::Panic
                           ? this->global_ctx.rt_funcs.at("rt_panic")
                           : this->global_ctx.rt_funcs.at("rt_panic_msg"));

    std::vector<llc::Value *> args;

    if (call.func->builtin_kind.value() == Function::BuiltinKind::PanicMsg)
      args.push_back(this->visit(call.args.at(0), ctx, insert));

    // const std::string &filename = call.loc.getSource()->getFilename();
    // auto [line, column] = call.loc.convertToLineAndColumn();
    std::string filename = "123";
    mpz_class line = 1;
    mpz_class column = 1;

    args.push_back(Str(ctx, insert, filename));
    args.push_back(U32(line));
    args.push_back(U32(column));

    return llc::Call(func, std::move(args));
  }
  case Function::BuiltinKind::Make:
  case Function::BuiltinKind::Take:
  case Function::BuiltinKind::Drop:
    assert(false && "not an intrinsic function");
  }

  assert(false);
}

void LLCGenerator::visit(const cfg::CompositeLiteralInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  const std::vector<cfg::Value *> &components = inst->components;
  size_t n = components.size();

  if (inst->reg->type->isStructType()) {
    llc::Type *llc_type = this->toLLCType(inst->reg->type);

    llc::Variable *temp = this->newTemporary(ctx, insert, llc_type);

    Struct *struct_ = inst->reg->type->toStructType()->getStruct();

    for (size_t i = 0, n = components.size(); i < n; ++i) {
      llc::Place *place = new llc::Place(temp, {this->global_ctx.struct_fields_map.at(&struct_->fields.at(i))});
      llc::Value *value = this->visit(components[i], ctx, insert);
      this->buildStore(insert, place, value);
    }

    ctx.mapRegister(inst->reg, this->buildLoad(ctx, insert, new llc::Place(temp)));
  } else if (inst->reg->type->isSliceType()) {
    assert(components.empty());

    llc::Type *slice_type = this->toLLCType(inst->reg->type);
    llc::Type *elem_type = this->toLLCType(inst->reg->type->toSliceType()->getElement());
    llc::Type *elem_ptr_type = llc::TypeProvider::Pointer(elem_type);

    llc::Variable *temp = this->newTemporary(ctx, insert, slice_type);

    llc::Place *data_place = new llc::Place(temp, {slice_type->toStructType()->fields.at(0)});
    this->buildStore(insert, data_place, new llc::NullValue(elem_ptr_type));

    llc::Place *length_place = new llc::Place(temp, {slice_type->toStructType()->fields.at(1)});
    this->buildStore(insert, length_place, U64(0));

    ctx.mapRegister(inst->reg, this->buildLoad(ctx, insert, new llc::Place(temp)));
  } else if (inst->reg->type->toArrayType()->isDynamic()) {
    llc::Type *arr_type = this->toLLCType(inst->reg->type);
    llc::Type *elem_type = this->toLLCType(inst->reg->type->toArrayType()->getElement());
    llc::Type *elem_ptr_type = llc::TypeProvider::Pointer(elem_type);

    llc::Variable *temp = this->newTemporary(ctx, insert, arr_type);

    llc::Place *data_place = new llc::Place(temp, {arr_type->toStructType()->fields.at(0)});

    if (n == 0) {
      this->buildStore(insert, data_place, new llc::NullValue(llc::TypeProvider::Ptr()));
    } else {
      llc::SizeofValue *sizeof_ = new llc::SizeofValue(llc::TypeProvider::UInt64(), elem_type);
      llc::IntConstantValue *num_elements = U64(n);
      llc::RegisterValue *num_bytes = this->buildBinaryOp(ctx, insert, llc::TypeProvider::UInt64(), llc::BinaryOp::Mul, sizeof_, num_elements)->asValue();

      llc::Call malloc_call = llc::Call(this->global_ctx.rt_funcs.at("rt_malloc"), {num_bytes});
      llc::RegisterValue *malloc_res = this->buildCall(ctx, insert, std::move(malloc_call))->asValue();

      llc::RegisterValue *cast = this->buildCast(ctx, insert, malloc_res, elem_ptr_type)->asValue();

      for (size_t i = 0; i < n; ++i) {
        llc::Value *llc_component = this->visit(components[i], ctx, insert);

        llc::Place *place = new llc::Place(llc::PlaceBase(llc::Deref(cast, U64(i))));
        this->buildStore(insert, place, llc_component);
      }

      this->buildStore(insert, data_place, cast);
    }

    llc::Place *length_place = new llc::Place(temp, {arr_type->toStructType()->fields.at(1)});
    this->buildStore(insert, length_place, U64(n));

    llc::Place *cap_place = new llc::Place(temp, {arr_type->toStructType()->fields.at(2)});
    this->buildStore(insert, cap_place, U64(n));

    llc::Place *elem_size_place = new llc::Place(temp, {arr_type->toStructType()->fields.at(3)});
    this->buildStore(insert, elem_size_place, new llc::SizeofValue(llc::TypeProvider::UInt64(), elem_type));

    ctx.mapRegister(inst->reg, this->buildLoad(ctx, insert, new llc::Place(temp)));
  } else {
    assert(!inst->reg->type->toArrayType()->isDynamic());
    llc::Type *arr_type = this->toLLCType(inst->reg->type);

    llc::Variable *temp = this->newTemporary(ctx, insert, arr_type);

    llc::Field *buffer_field = arr_type->toStructType()->fields.at(0);

    for (size_t i = 0, n = components.size(); i < n; ++i) {
      llc::Value *value = this->visit(components[i], ctx, insert);
      llc::Place *place = new llc::Place(temp, {buffer_field, U64(i)});
      this->buildStore(insert, place, value);
    }

    ctx.mapRegister(inst->reg, this->buildLoad(ctx, insert, new llc::Place(temp)));
  }
}

void LLCGenerator::visit(const cfg::NewInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Type *llc_alloc_type = this->toLLCType(inst->alloc_type);
  llc::SizeofValue *sizeof_ = new llc::SizeofValue(llc::TypeProvider::UInt64(), llc_alloc_type);

  llc::Call malloc_call = llc::Call(this->global_ctx.rt_funcs.at("rt_malloc"), {sizeof_});
  llc::RegisterValue *malloc_res = this->buildCall(ctx, insert, std::move(malloc_call))->asValue();

  llc::PointerType *target = llc::TypeProvider::Pointer(llc_alloc_type);
  llc::RegisterValue *cast = this->buildCast(ctx, insert, malloc_res, target)->asValue();
  ctx.mapRegister(inst->reg, cast->reg);

  llc::Place *place = new llc::Place(llc::PlaceBase(llc::Deref(cast, nullptr)));
  llc::Value *value = this->visit(inst->value, ctx, insert);
  this->buildStore(insert, place, value);
}

void LLCGenerator::visit(const cfg::PropertyAccessInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  assert(inst->property->name == "length");
  llc::Place *llc_place = this->visit(inst->base, ctx, insert);

  const llc::Type *type = llc_place->getType();
  llc::Field *length_field = type->toStructType()->fields.at(1);

  llc_place->projections.push_back(length_field);
  ctx.mapRegister(inst->reg, this->buildLoad(ctx, insert, llc_place));
}

void LLCGenerator::visit(const cfg::TypeTestInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Place *llc_place = this->visit(inst->place, ctx, insert);
  llc_place->projections.push_back(llc_place->getType()->toStructType()->fields.at(0));

  llc::RegisterValue *load_tag = this->buildLoad(ctx, insert, llc_place)->asValue();
  llc::BinaryOp op = inst->is_negated ? llc::BinaryOp::NE : llc::BinaryOp::EQ;

  llc::EnumConstantValue *target_tag = new llc::EnumConstantValue(llc_place->getType(), this->global_ctx.variant_tags_map.at(inst->alternative));

  ctx.mapRegister(inst->reg, this->buildBinaryOp(ctx, insert, llc::TypeProvider::Bool(), op, load_tag, target_tag));
}

void LLCGenerator::visit(const cfg::ValueCastInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Value *llc_value = this->visit(inst->value, ctx, insert);

  if (inst->target->isVariantType()) {
    const std::vector<Function *> &make_funcs = this->global_ctx.type_infos->at(inst->target).make_funcs;

    auto it = std::find_if(make_funcs.begin(), make_funcs.end(), [=](const Function *make) {
      return make->params.at(0).type->isEqualTo(inst->value->getType());
    });

    assert(it != make_funcs.end());

    llc::Function *func = this->global_ctx.funcs_map.at(*it);
    llc::Call call = llc::Call(func, {llc_value});
    ctx.mapRegister(inst->reg, this->buildCall(ctx, insert, std::move(call)));
  } else if (inst->value->getType()->isPointerType()) {
    ctx.mapRegister(inst->reg, llc_value->toRegisterValue()->reg);
  } else {
    ctx.mapRegister(inst->reg, this->buildCast(ctx, insert, llc_value, this->toLLCType(inst->target)));
  }
}

void LLCGenerator::visit(const cfg::PlaceCastInst *inst, FunctionContext &ctx, llc::BasicBlock *insert) {
  const Type *src = inst->place->getType();

  llc::Type *llc_src = this->toLLCType(src);
  llc::Type *llc_target = this->toLLCType(inst->target);

  llc::Variable *temp = this->newTemporary(ctx, insert, llc_target);

  if (src->isArrayType() && inst->target->isSliceType()) {
    if (src->toArrayType()->isDynamic()) {
      assert(false && "unimplemented");
    } else {
      llc::Place *llc_place1 = this->visit(inst->place, ctx, insert);
      llc_place1->projections.push_back(llc_src->toStructType()->fields.at(0));
      llc::Place *dst_data = new llc::Place(temp, {temp->type->toStructType()->fields.at(0)});

      llc::Variable *reg = ctx.newRegister(llc::TypeProvider::Pointer(this->toLLCType(src->toArrayType()->getElement())));
      insert->insts.push_back(new llc::LoadInst(reg, llc_place1));

      this->buildStore(insert, dst_data, reg->asValue());

      llc::Place *dst_length = new llc::Place(temp, {temp->type->toStructType()->fields.at(1)});
      this->buildStore(insert, dst_length, U64(src->toArrayType()->getLength()));
    }
  } else {
    assert(src->isStringType() && inst->target->isStrType());

    llc::Place *llc_place1 = this->visit(inst->place, ctx, insert);
    llc_place1->projections.push_back(llc_src->toStructType()->fields.at(0));
    llc::Place *dst_data = new llc::Place(temp, {temp->type->toStructType()->fields.at(0)});
    this->buildStore(insert, dst_data, this->buildLoad(ctx, insert, llc_place1)->asValue());

    llc::Place *llc_place2 = this->visit(inst->place, ctx, insert);
    llc_place2->projections.push_back(llc_src->toStructType()->fields.at(1));
    llc::Place *dst_length = new llc::Place(temp, {temp->type->toStructType()->fields.at(1)});
    this->buildStore(insert, dst_length, this->buildLoad(ctx, insert, llc_place2)->asValue());
  }

  ctx.mapRegister(inst->reg, this->buildLoad(ctx, insert, new llc::Place(temp)));
}

void LLCGenerator::visit(const cfg::Terminator *term, FunctionContext &ctx, llc::BasicBlock *insert) {
  switch (term->tag) {
  case cfg::Terminator::Tag::GotoTerm:          return this->visit(term->toGotoTerm(), ctx, insert);
  case cfg::Terminator::Tag::IfTerm:            return this->visit(term->toIfTerm(), ctx, insert);
  case cfg::Terminator::Tag::ReturnTerm:        return this->visit(term->toReturnTerm(), ctx, insert);
  case cfg::Terminator::Tag::SwitchTerm:        return this->visit(term->toSwitchTerm(), ctx, insert);
  case cfg::Terminator::Tag::VariantSwitchTerm: return this->visit(term->toVariantSwitchTerm(), ctx, insert);
  case cfg::Terminator::Tag::PanicTerm:         return this->visit(term->toPanicTerm(), ctx, insert);
  case cfg::Terminator::Tag::CallTerm:          return this->visit(term->toCallTerm(), ctx, insert);
  }

  assert(false);
}

void LLCGenerator::visit(const cfg::GotoTerm *term, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::BasicBlock *llc_target = ctx.blocks_map.at(term->target);
  this->buildGoto(insert, llc_target);
}

void LLCGenerator::visit(const cfg::IfTerm *term, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Value *llc_cond = this->visit(term->cond, ctx, insert);
  llc::BasicBlock *llc_then_bb = ctx.blocks_map.at(term->then_bb);
  llc::BasicBlock *llc_else_bb = ctx.blocks_map.at(term->else_bb);
  this->buildIf(insert, llc_cond, llc_then_bb, llc_else_bb);
}

void LLCGenerator::visit(const cfg::ReturnTerm *term, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Value *llc_value = term->value != nullptr ? this->visit(term->value, ctx, insert) : nullptr;
  this->buildReturn(insert, llc_value);
}

void LLCGenerator::visit(const cfg::SwitchTerm *term, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Value *llc_cond = this->visit(term->cond, ctx, insert);

  std::vector<llc::SwitchClause> llc_clauses;
  for (const cfg::SwitchClause &clause : term->clauses) {
    llc::BasicBlock *llc_target = ctx.blocks_map.at(clause.target);
    llc::Value *llc_label = this->visit(clause.label, ctx, insert);

    llc_clauses.emplace_back(llc_label, llc_target);
  }

  llc::BasicBlock *llc_default_target;
  if (term->default_target != nullptr) {
    llc_default_target = ctx.blocks_map.at(term->default_target);
  } else {
    llc_default_target = ctx.newBlock();

    // Insert after the last case target
    auto it = std::find(ctx.func->blocks.begin(), ctx.func->blocks.end(), llc_clauses.back().target);
    ctx.func->blocks.insert(it + 1, llc_default_target);

    this->panic(ctx, llc_default_target, term->loc, "exhaustive switch reached default clause");
  }

  this->buildSwitch(insert, llc_cond, std::move(llc_clauses), llc_default_target);
}

void LLCGenerator::visit(const cfg::VariantSwitchTerm *term, FunctionContext &ctx, llc::BasicBlock *insert) {
  const Type *type = term->cond->getType();
  assert(type->isVariantType());
  const llc::Type *llc_type = this->toLLCType(type);

  llc::Place *llc_place = this->visit(term->cond, ctx, insert);
  llc_place->projections.push_back(llc::Projection(llc_type->toStructType()->fields.at(0)));

  llc::RegisterValue *llc_cond = this->buildLoad(ctx, insert, llc_place)->asValue();

  std::vector<llc::SwitchClause> llc_clauses;
  for (const cfg::VariantSwitchClause &clause : term->clauses) {
    llc::BasicBlock *llc_target = ctx.blocks_map.at(clause.target);
    llc::EnumConstantValue *llc_label = new llc::EnumConstantValue(nullptr, this->global_ctx.variant_tags_map.at(clause.label));

    llc_clauses.emplace_back(llc_label, llc_target);
  }

  llc::BasicBlock *llc_default_target;
  if (term->default_target != nullptr) {
    llc_default_target = ctx.blocks_map.at(term->default_target);
  } else {
    llc_default_target = ctx.newBlock();

    // Insert after the last case target
    auto it = std::find(ctx.func->blocks.begin(), ctx.func->blocks.end(), llc_clauses.back().target);
    ctx.func->blocks.insert(it + 1, llc_default_target);

    this->panic(ctx, llc_default_target, term->loc, "exhaustive switch reached default clause");
  }

  this->buildSwitch(insert, llc_cond, std::move(llc_clauses), llc_default_target);
}

void LLCGenerator::panic(FunctionContext &ctx, llc::BasicBlock *insert, SourceLocation loc, std::string_view message) {
  llc::Function *panic = this->global_ctx.rt_funcs.at("rt_panic_msg");

  // const std::string &filename = loc.getSource()->getFilename();
  // auto [line, column] = loc.convertToLineAndColumn();
  std::string filename = "123";
  mpz_class line = 1;
  mpz_class column = 1;

  llc::Call panic_call = llc::Call(panic, {Str(ctx, insert, message), Str(ctx, insert, filename), U32(line), U32(column)});
  insert->setTerminator(new llc::CallTerm(std::move(panic_call)));
}

void LLCGenerator::visit(const cfg::PanicTerm *term, FunctionContext &ctx, llc::BasicBlock *insert) {
  this->panic(ctx, insert, term->loc, term->message);
}

void LLCGenerator::visit(const cfg::CallTerm *term, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Call call = this->lowerCall(term->call, ctx, insert);
  insert->setTerminator(new llc::CallTerm(std::move(call)));
}

llc::Place *LLCGenerator::visit(const cfg::Place *place, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Place *llc_place = new llc::Place(this->visit(place->base, ctx, insert));

  const Type *result = place->base.getType();

  for (const cfg::Projection &proj : place->projections) {
    switch (proj.getTag()) {
    case cfg::Projection::Tag::StructField: {
      const StructField *field = proj.get<StructField *>();
      llc_place->projections.push_back(this->global_ctx.struct_fields_map.at(field));
      result = proj.get<StructField *>()->type;
      continue;
    }
    case cfg::Projection::Tag::Index: {
      cfg::Value *idx = proj.get<cfg::Value *>();

      if (result->isArrayType() && !result->toArrayType()->isDynamic()) {
        llc_place->projections.push_back(this->toLLCType(result)->toStructType()->fields.at(0));
        llc_place->projections.push_back(this->visit(idx, ctx, insert));
        result = result->toArrayType()->getElement();
        continue;
      }

      // dynamic array / slice / string / str

      llc::Field *data_field = this->toLLCType(result)->toStructType()->fields.at(0);
      assert(data_field->name == "data");
      llc_place->projections.push_back(data_field);

      llc::RegisterValue *base = this->buildLoad(ctx, insert, llc_place)->asValue();

      if (result->isDynamicArray()) {
        // data field is ptr, cast to pointer type (to the array element)
        llc::Type *target = llc::TypeProvider::Pointer(this->toLLCType(result->toArrayType()->getElement()));
        base = this->buildCast(ctx, insert, base, target)->asValue();
      }

      llc_place = new llc::Place(llc::PlaceBase(llc::Deref(base, this->visit(idx, ctx, insert))));

      result = result->indexResult();
      continue;
    }
    case cfg::Projection::Tag::VariantAlternative: {
      const VariantAlternative *alt = proj.get<VariantAlternative *>();
      llc::Type *llc_variant = this->toLLCType(result);
      llc_place->projections.push_back(llc_variant->toStructType()->fields.at(1));
      llc_place->projections.push_back(this->global_ctx.variant_payload_fields_map.at(alt));
      result = alt->type;
      continue;
    }
    }

    assert(false);
  }

  return llc_place;
}

llc::PlaceBase LLCGenerator::visit(const cfg::PlaceBase &base, FunctionContext &ctx, llc::BasicBlock *insert) {
  switch (base.getTag()) {
  case cfg::PlaceBase::Tag::Variable:
    return llc::PlaceBase(ctx.vars_map.at(base.get<Variable *>()));
  case cfg::PlaceBase::Tag::Parameter: {
    Parameter *param = base.get<Parameter *>();

    if (param->is_return) {
      llc::RegisterValue *load = this->buildLoad(ctx, insert, new llc::Place(ctx.params_map.at(param)))->asValue();
      return llc::PlaceBase(llc::Deref(load, nullptr));
    } else {
      return llc::PlaceBase(ctx.params_map.at(param));
    }
  }
  case cfg::PlaceBase::Tag::Temporary:
    return llc::PlaceBase(ctx.temps_map.at(base.get<cfg::Temporary *>()));
  case cfg::PlaceBase::Tag::Deref: {
    llc::Value *llc_ptr = this->visit(base.get<cfg::Deref>().ptr, ctx, insert);
    return llc::PlaceBase(llc::PlaceBase(llc::Deref(llc_ptr, nullptr)));
  }
  }

  assert(false);
}

llc::Value *LLCGenerator::visit(const cfg::Value *value, FunctionContext &ctx, llc::BasicBlock *insert) {
  switch (value->tag) {
  case cfg::Value::Tag::RegisterValue: return this->visit(value->toRegisterValue(), ctx, insert);
  case cfg::Value::Tag::ConstantValue: return this->visit(value->toConstantValue(), ctx, insert);
  }

  assert(false);
}

llc::Value *LLCGenerator::visit(const cfg::RegisterValue *value, FunctionContext &ctx, llc::BasicBlock *) {
  llc::Variable *llc_reg = ctx.regs_map.at(value->reg);
  return new llc::RegisterValue(llc_reg->type, llc_reg);
}

llc::Value *LLCGenerator::visit(const cfg::ConstantValue *value, FunctionContext &ctx, llc::BasicBlock *insert) {
  llc::Type *llc_type = this->toLLCType(value->type);

  switch (value->constant->tag) {
  case Constant::Tag::IntegerConstant:
    return new llc::IntConstantValue(llc_type, value->constant->toIntegerConstant()->value);
  case Constant::Tag::BoolConstant:
    return new llc::BoolConstantValue(llc_type, value->constant->toBoolConstant()->value);
  case Constant::Tag::CharConstant:
    return new llc::CharConstantValue(llc_type, value->constant->toCharConstant()->value);
  case Constant::Tag::EnumConstant: {
    llc::EnumValue *llc_value = this->global_ctx.enum_values_map.at(value->constant->toEnumConstant()->value);
    return new llc::EnumConstantValue(llc_type, llc_value);
  }
  case Constant::Tag::NullConstant:
    return new llc::NullValue(llc_type);
  case Constant::Tag::StringConstant: {
    return this->lowerStringConstant(value, ctx, insert);
  }
  }

  assert(false);
}

llc::Value *LLCGenerator::lowerStringConstant(const cfg::ConstantValue *value, FunctionContext &ctx, llc::BasicBlock *insert) {
  const std::string &str = value->toConstantValue()->constant->toStringConstant()->value;
  const Type *type = value->type;
  llc::Type *llc_type = this->toLLCType(type);

  if (type->isStringType()) {
    llc::Variable *temp = this->newTemporary(ctx, insert, llc_type);

    llc::Field *data_field = llc_type->toStructType()->fields.at(0);
    llc::Field *length_field = llc_type->toStructType()->fields.at(1);
    llc::Field *capacity_field = llc_type->toStructType()->fields.at(2);

    llc::Place *data_place = new llc::Place(temp, {data_field});
    llc::Place *length_place = new llc::Place(temp, {length_field});
    llc::Place *capacity_place = new llc::Place(temp, {capacity_field});

    if (str.empty()) {
      this->buildStore(insert, length_place, U64(0));
      this->buildStore(insert, capacity_place, U64(0));
      this->buildStore(insert, data_place, new llc::NullValue(data_field->type));
    } else {
      size_t length = str.size();

      this->buildStore(insert, length_place, U64(length));
      this->buildStore(insert, capacity_place, U64(length));

      llc::Call malloc_call = llc::Call(this->global_ctx.rt_funcs.at("rt_malloc"), {U64(length)});
      llc::RegisterValue *malloc_res = this->buildCall(ctx, insert, std::move(malloc_call))->asValue();
      llc::RegisterValue *cast = this->buildCast(ctx, insert, malloc_res, data_field->type)->asValue();

      this->buildStore(insert, data_place, cast);

      llc::GlobalStringConstant *global_string = this->getOrCreateGlobalStringConstant(str);
      llc::Value *global_value = new llc::GlobalStringValue(llc::TypeProvider::Pointer(llc::TypeProvider::Char()), global_string);

      llc::Call memcpy_call = llc::Call(this->global_ctx.rt_funcs.at("rt_memcpy"), {
        this->buildLoad(ctx, insert, data_place)->asValue(),
        global_value,
        U64(length)
      });
      this->buildVoidCall(insert, std::move(memcpy_call));
    }

    return this->buildLoad(ctx, insert, new llc::Place(temp))->asValue();
  } else if (type->isStrType()) {
    llc::Variable *temp = this->newTemporary(ctx, insert, llc_type);

    llc::Field *data_field = llc_type->toStructType()->fields.at(0);
    llc::Field *length_field = llc_type->toStructType()->fields.at(1);

    llc::Place *data_place = new llc::Place(temp, {data_field});
    llc::Place *length_place = new llc::Place(temp, {length_field});

    if (str.empty()) {
      this->buildStore(insert, data_place, new llc::NullValue(data_field->type));
      this->buildStore(insert, length_place, U64(0));
    } else {
      llc::GlobalStringConstant *global_string = this->getOrCreateGlobalStringConstant(str);
      llc::Value *global_value = new llc::GlobalStringValue(data_field->type, global_string);
      this->buildStore(insert, data_place, global_value);

      this->buildStore(insert, length_place, U64(str.size()));
    }

    return this->buildLoad(ctx, insert, new llc::Place(temp))->asValue();
  } else {
    assert(false && "unimplemented");
  }
}

llc::Value *LLCGenerator::Str(FunctionContext &ctx, llc::BasicBlock *insert, std::string_view s) {
  llc::Type *llc_str = this->toLLCType(TypeProvider::Str());

  llc::Variable *temp = this->newTemporary(ctx, insert, llc_str);

  llc::Field *length_field = llc_str->toStructType()->fields.at(1);
  llc::Place *length_place = new llc::Place(temp, {length_field});
  this->buildStore(insert, length_place, U64(s.size()));

  llc::GlobalStringConstant *global_string = this->getOrCreateGlobalStringConstant(std::string(s));
  llc::Variable *reg = ctx.newRegister(llc::TypeProvider::Pointer(llc::TypeProvider::Char()));
  llc::LoadInst *load_inst = new llc::LoadInst(reg, new llc::Place(global_string->global));
  llc::RegisterValue *load = this->build(insert, load_inst)->asValue();

  llc::Place *data_place = new llc::Place(temp, {llc_str->toStructType()->fields.at(0)});
  this->buildStore(insert, data_place, load);

  return this->buildLoad(ctx, insert, new llc::Place(llc::PlaceBase(temp)))->asValue();
}

llc::GlobalStringConstant *LLCGenerator::getOrCreateGlobalStringConstant(const std::string &value) {
  auto it = this->global_ctx.strings_map.find(value);

  if (it != this->global_ctx.strings_map.end())
    return it->second;

  std::string name = "_s" + std::to_string(this->global_ctx.string_constants.size() + 1);
  llc::Type *type = llc::TypeProvider::Array(llc::TypeProvider::Char(), value.size());
  llc::Variable *global = new llc::Variable(std::move(name), type);
  llc::GlobalStringConstant *constant = new llc::GlobalStringConstant(global, value);

  this->global_ctx.strings_map.insert({value, constant});
  this->global_ctx.string_constants.push_back(constant);

  return constant;
}

// Instruction builders

void LLCGenerator::buildVoidCall(llc::BasicBlock *insert, llc::Call call) {
  insert->insts.push_back(new llc::VoidCallInst(std::move(call)));
}

void LLCGenerator::buildStore(llc::BasicBlock *insert, llc::Place *place, llc::Value *value) {
  insert->insts.push_back(new llc::StoreInst(place, value));
}

// Value instructions builders

llc::Variable *LLCGenerator::buildUnaryOp(FunctionContext &ctx, llc::BasicBlock *insert, llc::Type *type, llc::UnaryOp op, llc::Value *operand) {
  return this->build(insert, new llc::UnaryOpInst(ctx.newRegister(type), op, operand));
}

llc::Variable *LLCGenerator::buildAddressOf(FunctionContext &ctx, llc::BasicBlock *insert, llc::Place *place) {
  return this->build(insert, new llc::AddressOfInst(ctx.newRegister(llc::TypeProvider::Pointer(const_cast<llc::Type *>(place->getType()))), place));
}

llc::Variable *LLCGenerator::buildLoad(FunctionContext &ctx, llc::BasicBlock *insert, llc::Place *place) {
  return this->build(insert, new llc::LoadInst(ctx.newRegister(place->getType()), place));
}

llc::Variable *LLCGenerator::buildBinaryOp(FunctionContext &ctx, llc::BasicBlock *insert, llc::Type *type, llc::BinaryOp op, llc::Value *left, llc::Value *right) {
  return this->build(insert, new llc::BinaryOpInst(ctx.newRegister(type), op, left, right));
}

llc::Variable *LLCGenerator::buildCall(FunctionContext &ctx, llc::BasicBlock *insert, llc::Call call) {
  return this->build(insert, new llc::CallInst(ctx.newRegister(call.func->return_type), std::move(call)));
}

llc::Variable *LLCGenerator::buildCast(FunctionContext &ctx, llc::BasicBlock *insert, llc::Value *value, llc::Type *target) {
  return this->build(insert, new llc::CastInst(ctx.newRegister(target), value, target));
}

// Terminator builders

void LLCGenerator::buildGoto(llc::BasicBlock *insert, llc::BasicBlock *target) {
  insert->setTerminator(new llc::GotoTerm(target));
}

void LLCGenerator::buildIf(llc::BasicBlock *insert, llc::Value *cond, llc::BasicBlock *then_bb, llc::BasicBlock *else_bb) {
  insert->setTerminator(new llc::IfTerm(cond, then_bb, else_bb));
}

void LLCGenerator::buildReturn(llc::BasicBlock *insert, llc::Value *value) {
  insert->setTerminator(new llc::ReturnTerm(value));
}

void LLCGenerator::buildSwitch(llc::BasicBlock *insert, llc::Value *cond, std::vector<llc::SwitchClause> clauses, llc::BasicBlock *default_target) {
  insert->setTerminator(new llc::SwitchTerm(cond, std::move(clauses), default_target));
}
