#include "constant.hpp"

#include <cassert>

#include "acl.hpp"
#include "constant-provider.hpp"
#include "gmpxx.h"
#include "type.hpp"
#include "../util/util.hpp"

std::string Constant::toString() const {
  switch (this->tag) {
  case Tag::IntegerConstant:
    return this->toIntegerConstant()->value.get_str();
  case Tag::BoolConstant:
    return this->toBoolConstant()->value ? "true" : "false";
  case Tag::CharConstant:
    return charValueToLiteral(this->toCharConstant()->value);
  case Tag::StringConstant:
    return stringValueToLiteral(this->toStringConstant()->value);
  case Tag::EnumConstant:
    return this->toEnumConstant()->value->name;
  case Tag::NullConstant:
    return "null";
  default:
    assert(false);
  }
}

const Constant *Constant::convert(const Type *type, const Type *target) const {
  if (this->isIntegerConstant()) {
    assert(type->isIntegerType());

    if (type->isImplicitlyConvertibleTo(target))
      return this;

    const mpz_class &value = this->toIntegerConstant()->value;

    if (target->isIntegerType()) {
      if (type->toIntegerType()->isSigned() == target->toIntegerType()->isSigned()) {
        unsigned target_bits = target->toIntegerType()->getBits();
        mpz_class mod = mpz_class(1) << target_bits;

        mpz_class v = value % mod;
        if (v < 0)
          v += mod;

        if (target->toIntegerType()->isSigned()) {
          mpz_class half = mpz_class(1) << (target_bits - 1);
          if (v >= half)
            v -= mod;
        }

        return ConstantProvider::Integer(std::move(v));
      } else {
        unsigned bits = type->toIntegerType()->getBits();
        mpz_class mod = mpz_class(1) << bits;

        mpz_class v = value % mod;
        if (v < 0)
          v += mod;

        if (target->toIntegerType()->isSigned()) {
          mpz_class half = mpz_class(1) << (bits - 1);
          if (v >= half)
            v -= mod;
        }

        return ConstantProvider::Integer(std::move(v));
      }
    }

    if (target->isCharType()) {
      char v = this->toIntegerConstant()->value.get_ui();
      return ConstantProvider::Char(v);
    }

    if (target->isEnumType()) {
      assert(false);
    }

    assert(false);
  }

  if (this->isCharConstant()) {
    assert(type->isCharType());

    if (target->isIntegerType()) {
      uint8_t v = this->toCharConstant()->value;
      return ConstantProvider::Integer(v);
    }

    assert(false);
  }

  assert(false);
}

bool IntegerConstant::fitsIntegerType(const IntegerType *type) const {
  if (type->isSigned()) {
    mpz_class min, max;

    // min = -2^(bits - 1)
    mpz_ui_pow_ui(min.get_mpz_t(), 2, type->getBits() - 1);
    min = -min;
    // max = 2^(bits - 1) - 1
    mpz_ui_pow_ui(max.get_mpz_t(), 2, type->getBits() - 1);
    max -= 1;

    return min <= value && value <= max;
  } else {
    if (value < 0)
      return false;

    mpz_class max;
    // max = 2^bits - 1
    mpz_ui_pow_ui(max.get_mpz_t(), 2, type->getBits());
    max -= 1;

    return value <= max;
  }
}
