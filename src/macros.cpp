#include "./frontend/acl.hpp"
#include "./frontend/constant.hpp"
#include "./frontend/type.hpp"
#include "./CFG/cfg.hpp"
#include "./LLC/llc.hpp"

#define X(TypeName_)                                                            \
  bool TypeName::is##TypeName_() const { return this->tag == Tag::TypeName_; }  \
  TypeName_ *TypeName::to##TypeName_() {                                        \
    assert(this->is##TypeName_());                                              \
    return static_cast<TypeName_ *>(this);                                      \
  }                                                                             \
  const TypeName_ *TypeName::to##TypeName_() const {                            \
    assert(this->is##TypeName_());                                              \
    return static_cast<const TypeName_ *>(this);                                \
  }
#include "./defs/type-name-tags.def"
#undef X

#define X(TypeName)                                                       \
  bool Stmt::is##TypeName() const { return this->tag == Tag::TypeName; }  \
  TypeName *Stmt::to##TypeName() {                                        \
    assert(this->is##TypeName());                                         \
    return static_cast<TypeName *>(this);                                 \
  }                                                                       \
  const TypeName *Stmt::to##TypeName() const {                            \
    assert(this->is##TypeName());                                         \
    return static_cast<const TypeName *>(this);                           \
  }
#include "./defs/stmt-tags.def"
#undef X

#define X(TypeName)                                                       \
  bool Expr::is##TypeName() const { return this->tag == Tag::TypeName; }  \
  TypeName *Expr::to##TypeName() {                                        \
    assert(this->is##TypeName());                                         \
    return static_cast<TypeName *>(this);                                 \
  }                                                                       \
  const TypeName *Expr::to##TypeName() const {                            \
    assert(this->is##TypeName());                                         \
    return static_cast<const TypeName *>(this);                           \
  }
#include "./defs/expr-tags.def"
#undef X

#define X(TypeName)                                                           \
  bool Constant::is##TypeName() const { return this->tag == Tag::TypeName; }  \
  TypeName *Constant::to##TypeName() {                                        \
    assert(this->is##TypeName());                                             \
    return static_cast<TypeName *>(this);                                     \
  }                                                                           \
  const TypeName *Constant::to##TypeName() const {                            \
    assert(this->is##TypeName());                                             \
    return static_cast<const TypeName *>(this);                               \
  }
#include "./defs/constant-tags.def"
#undef X

#define X(TypeName)                                                       \
  bool Type::is##TypeName() const { return this->tag == Tag::TypeName; }  \
  TypeName *Type::to##TypeName() {                                        \
    assert(this->is##TypeName());                                         \
    return static_cast<TypeName *>(this);                                 \
  }                                                                       \
  const TypeName *Type::to##TypeName() const {                            \
    assert(this->is##TypeName());                                         \
    return static_cast<const TypeName *>(this);                           \
  }
#include "./defs/type-tags.def"
#undef X

#define X(TypeName)                                                                   \
  bool cfg::Instruction::is##TypeName() const { return this->tag == Tag::TypeName; }  \
  cfg::TypeName *cfg::Instruction::to##TypeName() {                                   \
    assert(this->is##TypeName());                                                     \
    return static_cast<cfg::TypeName *>(this);                                        \
  }                                                                                   \
  const cfg::TypeName *cfg::Instruction::to##TypeName() const {                       \
    assert(this->is##TypeName());                                                     \
    return static_cast<const cfg::TypeName *>(this);                                  \
  }
#include "./defs/cfg-instruction-tags.def"
#undef X

#define X(TypeName)                                                                  \
  bool cfg::Terminator::is##TypeName() const { return this->tag == Tag::TypeName; }  \
  cfg::TypeName *cfg::Terminator::to##TypeName() {                                   \
    assert(this->is##TypeName());                                                    \
    return static_cast<cfg::TypeName *>(this);                                       \
  }                                                                                  \
  const cfg::TypeName *cfg::Terminator::to##TypeName() const {                       \
    assert(this->is##TypeName());                                                    \
    return static_cast<const cfg::TypeName *>(this);                                 \
  }
#include "./defs/cfg-terminator-tags.def"
#undef X

#define X(TypeName)                                                             \
  bool cfg::Value::is##TypeName() const { return this->tag == Tag::TypeName; }  \
  cfg::TypeName *cfg::Value::to##TypeName() {                                   \
    assert(this->is##TypeName());                                               \
    return static_cast<cfg::TypeName *>(this);                                  \
  }                                                                             \
  const cfg::TypeName *cfg::Value::to##TypeName() const {                       \
    assert(this->is##TypeName());                                               \
    return static_cast<const cfg::TypeName *>(this);                            \
  }
#include "./defs/cfg-value-tags.def"
#undef X

#define X(TypeName)                                                            \
  bool llc::Type::is##TypeName() const { return this->tag == Tag::TypeName; }  \
  llc::TypeName *llc::Type::to##TypeName() {                                   \
    assert(this->is##TypeName());                                              \
    return static_cast<llc::TypeName *>(this);                                 \
  }                                                                            \
  const llc::TypeName *llc::Type::to##TypeName() const {                       \
    assert(this->is##TypeName());                                              \
    return static_cast<const llc::TypeName *>(this);                           \
  }
#include "./defs/llc-type-tags.def"
#undef X

#define X(TypeName)                                                                   \
  bool llc::Instruction::is##TypeName() const { return this->tag == Tag::TypeName; }  \
  llc::TypeName *llc::Instruction::to##TypeName() {                                   \
    assert(this->is##TypeName());                                                     \
    return static_cast<llc::TypeName *>(this);                                        \
  }                                                                                   \
  const llc::TypeName *llc::Instruction::to##TypeName() const {                       \
    assert(this->is##TypeName());                                                     \
    return static_cast<const llc::TypeName *>(this);                                  \
  }
#include "./defs/llc-instruction-tags.def"
#undef X

#define X(TypeName)                                                                  \
  bool llc::Terminator::is##TypeName() const { return this->tag == Tag::TypeName; }  \
  llc::TypeName *llc::Terminator::to##TypeName() {                                   \
    assert(this->is##TypeName());                                                    \
    return static_cast<llc::TypeName *>(this);                                       \
  }                                                                                  \
  const llc::TypeName *llc::Terminator::to##TypeName() const {                       \
    assert(this->is##TypeName());                                                    \
    return static_cast<const llc::TypeName *>(this);                                 \
  }
#include "./defs/llc-terminator-tags.def"
#undef X

#define X(TypeName)                                                             \
  bool llc::Value::is##TypeName() const { return this->tag == Tag::TypeName; }  \
  llc::TypeName *llc::Value::to##TypeName() {                                   \
    assert(this->is##TypeName());                                               \
    return static_cast<llc::TypeName *>(this);                                  \
  }                                                                             \
  const llc::TypeName *llc::Value::to##TypeName() const {                       \
    assert(this->is##TypeName());                                               \
    return static_cast<const llc::TypeName *>(this);                            \
  }
#include "./defs/llc-value-tags.def"
#undef X
