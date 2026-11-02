#include "constant-provider.hpp"

BoolConstant ConstantProvider::true_ = BoolConstant(true);
BoolConstant ConstantProvider::false_ = BoolConstant(false);
NullConstant ConstantProvider::null_;

std::unordered_map<mpz_class, std::unique_ptr<IntegerConstant>, MpzClassHash, MpzClassEqual> ConstantProvider::integer_constants;
std::unordered_map<std::string, std::unique_ptr<StringConstant>> ConstantProvider::string_constants;
std::unordered_map<char, std::unique_ptr<CharConstant>> ConstantProvider::char_constants;
std::unordered_map<EnumValue *, std::unique_ptr<EnumConstant>> ConstantProvider::enum_constants;

const BoolConstant *ConstantProvider::Bool(bool b) {
  return b ? &true_ : &false_;
}

const NullConstant *ConstantProvider::Null() {
  return &null_;
}

const CharConstant *ConstantProvider::Char(char c) {
  auto it = char_constants.find(c);
  if (it != char_constants.end())
    return it->second.get();

  std::unique_ptr<CharConstant> cst = std::unique_ptr<CharConstant>(new CharConstant(c));
  char_constants[c] = std::move(cst);
  return char_constants[c].get();
}

const IntegerConstant *ConstantProvider::Integer(const mpz_class &v) {
  auto it = integer_constants.find(v);
  if (it != integer_constants.end())
    return it->second.get();

  std::unique_ptr<IntegerConstant> cst = std::unique_ptr<IntegerConstant>(new IntegerConstant(v));
  integer_constants[v] = std::move(cst);
  return integer_constants[v].get();
}

const StringConstant *ConstantProvider::String(const std::string &s) {
  auto it = string_constants.find(s);
  if (it != string_constants.end())
    return it->second.get();

  std::unique_ptr<StringConstant> cst = std::unique_ptr<StringConstant>(new StringConstant(s));
  string_constants[s] = std::move(cst);
  return string_constants[s].get();
}

const EnumConstant *ConstantProvider::Enum(EnumValue *value) {
  auto it = enum_constants.find(value);
  if (it != enum_constants.end())
    return it->second.get();

  std::unique_ptr<EnumConstant> cst = std::unique_ptr<EnumConstant>(new EnumConstant(value));
  enum_constants[value] = std::move(cst);
  return enum_constants[value].get();
}
