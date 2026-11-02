#pragma once

#include <memory>
#include <unordered_map>
#include <string>

#include "constant.hpp"
#include "gmpxx.h"

struct MpzClassHash {
  size_t operator()(const mpz_class &x) const {
    std::string s = x.get_str();
    return std::hash<std::string>{}(s);
  }
};

struct MpzClassEqual {
  bool operator()(const mpz_class &a, const mpz_class &b) const {
    return a == b;
  }
};

class ConstantProvider {
 public:
  static const BoolConstant *Bool(bool b);
  static const NullConstant *Null();
  static const CharConstant *Char(char c);
  static const IntegerConstant *Integer(const mpz_class &v);
  static const StringConstant *String(const std::string &s);
  static const EnumConstant *Enum(EnumValue *value);

 private:
  static BoolConstant true_;
  static BoolConstant false_;
  static NullConstant null_;

  static std::unordered_map<mpz_class, std::unique_ptr<IntegerConstant>, MpzClassHash, MpzClassEqual> integer_constants;
  static std::unordered_map<std::string, std::unique_ptr<StringConstant>> string_constants;
  static std::unordered_map<char, std::unique_ptr<CharConstant>> char_constants;
  static std::unordered_map<EnumValue *, std::unique_ptr<EnumConstant>> enum_constants;
};
