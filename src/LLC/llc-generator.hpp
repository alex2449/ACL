#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "../CFG/cfg.hpp"
#include "../frontend/acl.hpp"
#include "../frontend/type.hpp"

#include "llc.hpp"
#include "llc-type-provider.hpp"

struct Function;

struct FunctionContext {
  FunctionContext(llc::Function *func) : func(func) { }

  llc::Variable *newVariable(const std::string &name, const llc::Type *type) {
    unsigned id = this->next_variable_id++;
    return new llc::Variable(name + "_" + std::to_string(id), type);
  }

  llc::Variable *newTemporary(const llc::Type *type) {
    unsigned id = this->next_temporary_id++;
    return new llc::Variable("_t" + std::to_string(id), type);
  }

  llc::Variable *newRegister(const llc::Type *type) {
    unsigned id = this->next_register_id++;
    return new llc::Variable("_r" + std::to_string(id), type);
  }

  llc::BasicBlock *newBlock() {
    return new llc::BasicBlock(this->next_block_id++);
  }

  void mapRegister(cfg::Register *cfg_reg, llc::Variable *llc_reg) {
    this->regs_map.insert({cfg_reg, llc_reg});
  }

  llc::Function *func;

  std::unordered_map<const Parameter *, llc::Parameter *> params_map;
  std::unordered_map<const Variable *, llc::Variable *> vars_map;
  std::unordered_map<const cfg::Temporary *, llc::Variable *> temps_map;
  std::unordered_map<const cfg::Register *, llc::Variable *> regs_map;
  std::unordered_map<const cfg::BasicBlock *, llc::BasicBlock *> blocks_map;

  unsigned next_variable_id = 0;
  unsigned next_temporary_id = 0;
  unsigned next_register_id = 0;
  unsigned next_block_id = 0;
};

struct GlobalContext {
  GlobalContext(std::vector<Function *> *funcs,
                std::unordered_map<const Type *, TypeInfo> *type_infos,
                std::unordered_map<const Function *, cfg::CFG> *cfg_map)
      : funcs(funcs), type_infos(type_infos), cfg_map(cfg_map) { }

  std::vector<Function *> *funcs;
  std::unordered_map<const Type *, TypeInfo> *type_infos;
  std::unordered_map<const Function *, cfg::CFG> *cfg_map;

  llc::StructType *dyn_arr = nullptr;

  std::vector<llc::Type *> types;
  std::vector<llc::Function *> llc_funcs;
  std::vector<llc::GlobalStringConstant *> string_constants;

  std::unordered_map<std::string, llc::Function *> rt_funcs;
  std::unordered_map<const Function *, llc::Function *> funcs_map;
  std::unordered_map<const Type *, llc::Type *> types_map;
  std::unordered_map<const Type *, llc::Function *> type_deleters_map;
  std::unordered_map<const StructField *, llc::Field *> struct_fields_map;
  std::unordered_map<const VariantAlternative *, llc::EnumValue *> variant_tags_map;
  std::unordered_map<const VariantAlternative *, llc::Field *> variant_payload_fields_map;
  std::unordered_map<const EnumValue *, llc::EnumValue *> enum_values_map;
  std::unordered_map<std::string, llc::GlobalStringConstant *> strings_map;
};

class LLCGenerator {
 public:
  LLCGenerator(std::vector<Function *> *funcs,
               std::unordered_map<const Type *, TypeInfo> *type_infos,
               std::unordered_map<const Function *, cfg::CFG> *cfg_map)
      : global_ctx(GlobalContext(funcs, type_infos, cfg_map)) { }

  llc::Module generate();

 private:
  void registerBuiltinTypes();
  void declareRuntime();

  llc::Type *toLLCType(const Type *type);
  llc::StructType *getOrCreateDynArr();
  void declareType(const Type *type);
  void defineType(const Type *type);
  llc::EnumType *declareVariantTag(const Variant *variant);
  llc::UnionType *declareVariantPayload(const Variant *variant);

  void declareFunction(const Function *func);
  void declareDeleter(const Type *type);

  void defineFunction(const Function *func);
  void defineDeleter(const Type *type);

  void defineUser(const Function *func);
  void defineMake(const Function *func);
  void defineStructMake(const Function *func);
  void defineVariantMake(const Function *func);
  void defineTake(const Function *func);
  void defineArrayTake(const Function *func);
  void defineStructTake(const Function *func);
  void defineVariantTake(const Function *func);
  llc::Variable *take(FunctionContext &ctx, const Type *type, llc::Place *place, llc::BasicBlock *insert);
  void defineDrop(const Function *func);
  void defineArrayDrop(const Function *func);
  void defineStructDrop(const Function *func);
  void defineVariantDrop(const Function *func);
  void drop(FunctionContext &ctx, const Type *type, llc::Place *place, llc::BasicBlock *insert);
  void drop(FunctionContext &ctx, const Type *type, llc::Value *value, llc::BasicBlock *insert);

  void visit(const cfg::Instruction *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::VoidCallInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::BeginStorageInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::EndStorageInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  llc::Place *storageToPlace(const cfg::Storage *storage, FunctionContext &ctx);
  void visit(const cfg::DropValInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::InitPlaceInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::AssignPlaceInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);

  void visit(const cfg::UnaryOpInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::AddressOfInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::LoadInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::TakeInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::BinaryOpInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::CallInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  llc::Call lowerCall(const cfg::Call &call, FunctionContext &ctx, llc::BasicBlock *insert);
  llc::Call lowerIntrinsicCall(const cfg::Call &call, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::CompositeLiteralInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::NewInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::PropertyAccessInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::TypeTestInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::ValueCastInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::PlaceCastInst *inst, FunctionContext &ctx, llc::BasicBlock *insert);

  void visit(const cfg::Terminator *term, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::GotoTerm *term, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::IfTerm *term, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::ReturnTerm *term, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::SwitchTerm *term, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::VariantSwitchTerm *term, FunctionContext &ctx, llc::BasicBlock *insert);
  void visit(const cfg::PanicTerm *term, FunctionContext &ctx, llc::BasicBlock *insert);
  void panic(FunctionContext &ctx, llc::BasicBlock *insert, SourceLocation loc, std::string_view message);
  void visit(const cfg::CallTerm *term, FunctionContext &ctx, llc::BasicBlock *insert);

  llc::Place *visit(const cfg::Place *place, FunctionContext &ctx, llc::BasicBlock *insert);
  llc::PlaceBase visit(const cfg::PlaceBase &base, FunctionContext &ctx, llc::BasicBlock *insert);

  llc::Value *visit(const cfg::Value *value, FunctionContext &ctx, llc::BasicBlock *insert);
  llc::Value *visit(const cfg::RegisterValue *value, FunctionContext &ctx, llc::BasicBlock *insert);
  llc::Value *visit(const cfg::ConstantValue *value, FunctionContext &ctx, llc::BasicBlock *insert);
  llc::Value *lowerStringConstant(const cfg::ConstantValue *value, FunctionContext &ctx, llc::BasicBlock *insert);
  llc::Value *Str(FunctionContext &ctx, llc::BasicBlock *insert, std::string_view s);
  llc::GlobalStringConstant *getOrCreateGlobalStringConstant(const std::string &value);

  llc::Variable *newTemporary(FunctionContext &ctx, llc::BasicBlock *insert, const llc::Type *type) {
    llc::Variable *temp = ctx.newTemporary(type);
    build(insert, new llc::VarDeclInst(temp));
    return temp;
  }

  // Instructions builders
  static void build(llc::BasicBlock *insert, llc::Instruction *inst) {
    insert->insts.push_back(inst);
  }
  void buildVoidCall(llc::BasicBlock *insert, llc::Call call);
  void buildStore(llc::BasicBlock *insert, llc::Place *place, llc::Value *value);
  // Value instructions builders
  static llc::Variable *build(llc::BasicBlock *insert, llc::ValueInst *inst) {
    insert->insts.push_back(inst);
    return inst->reg;
  }
  llc::Variable *buildUnaryOp(FunctionContext &ctx, llc::BasicBlock *insert, llc::Type *type, llc::UnaryOp op, llc::Value *operand);
  llc::Variable *buildAddressOf(FunctionContext &ctx, llc::BasicBlock *insert, llc::Place *place);
  llc::Variable *buildLoad(FunctionContext &ctx, llc::BasicBlock *insert, llc::Place *place);
  llc::Variable *buildBinaryOp(FunctionContext &ctx, llc::BasicBlock *insert, llc::Type *type, llc::BinaryOp op, llc::Value *left, llc::Value *right);
  llc::Variable *buildCall(FunctionContext &ctx, llc::BasicBlock *insert, llc::Call call);
  llc::Variable *buildCast(FunctionContext &ctx, llc::BasicBlock *insert, llc::Value *value, llc::Type *target);
  // Terminator builders
  void buildGoto(llc::BasicBlock *insert, llc::BasicBlock *target);
  void buildIf(llc::BasicBlock *insert, llc::Value *cond, llc::BasicBlock *then_bb, llc::BasicBlock *else_bb);
  void buildReturn(llc::BasicBlock *insert, llc::Value *value);
  void buildSwitch(llc::BasicBlock *insert, llc::Value *cond, std::vector<llc::SwitchClause> clauses, llc::BasicBlock *default_target);
  // void buildCallTerm(llc::BasicBlock *insert, llc::Call call);

  static llc::IntConstantValue *U32(mpz_class value) {
    return new llc::IntConstantValue(llc::TypeProvider::UInt32(), std::move(value));
  }

  static llc::IntConstantValue *U64(mpz_class value) {
    return new llc::IntConstantValue(llc::TypeProvider::UInt64(), std::move(value));
  }

  GlobalContext global_ctx;

  // Diagnostics *diag;

  std::unordered_map<cfg::UnaryOp, llc::UnaryOp> unary_ops_map = {
    { cfg::UnaryOp::Minus,  llc::UnaryOp::Minus  },
    { cfg::UnaryOp::Not,    llc::UnaryOp::Not    },
    { cfg::UnaryOp::BitNot, llc::UnaryOp::BitNot }
  };

  std::unordered_map<cfg::BinaryOp, llc::BinaryOp> binary_ops_map = {
    { cfg::BinaryOp::Add,    llc::BinaryOp::Add    },
    { cfg::BinaryOp::Sub,    llc::BinaryOp::Sub    },
    { cfg::BinaryOp::Mul,    llc::BinaryOp::Mul    },
    { cfg::BinaryOp::Div,    llc::BinaryOp::Div    },
    { cfg::BinaryOp::Rem,    llc::BinaryOp::Rem    },
    { cfg::BinaryOp::LT,     llc::BinaryOp::LT     },
    { cfg::BinaryOp::LE,     llc::BinaryOp::LE     },
    { cfg::BinaryOp::GT,     llc::BinaryOp::GT     },
    { cfg::BinaryOp::GE,     llc::BinaryOp::GE     },
    { cfg::BinaryOp::EQ,     llc::BinaryOp::EQ     },
    { cfg::BinaryOp::NE,     llc::BinaryOp::NE     },
    { cfg::BinaryOp::BitAnd, llc::BinaryOp::BitAnd },
    { cfg::BinaryOp::BitOr,  llc::BinaryOp::BitOr  },
    { cfg::BinaryOp::BitXor, llc::BinaryOp::BitXor },
    { cfg::BinaryOp::SHL,    llc::BinaryOp::SHL    },
    { cfg::BinaryOp::SHR,    llc::BinaryOp::SHR    },
  };
};
