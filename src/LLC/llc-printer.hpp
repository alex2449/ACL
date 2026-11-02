#pragma once

#include <iostream>
#include <string_view>
#include <unordered_set>

#include "llc.hpp"

class LLCPrinter {
 public:
  LLCPrinter(std::ostream &os, int tabsize) : os(os), tabsize(tabsize) { }

  void visit(const llc::Module *module);

 private:
  void printAsDeclaration(const llc::Function *function) const;
  void printAsDefinition(const llc::Function *function) const;
  void printFunctionHeader(const llc::Function *function) const;
  void visit(const llc::BasicBlock *block) const;
  void printType(const llc::Type *type, std::unordered_set<const llc::Type *> *visited);
  void printAsTypeDecl(const llc::Type *type) const;
  void printStructOrEnumDecl(std::string_view keyword,
                             std::string_view name,
                             const std::vector<llc::Field *> &fields) const;
  void printAsTypeDef(const llc::Type *type) const;
  void printAsTypeName(const llc::Type *type) const;
  void visit(const llc::Instruction *inst) const;
  void printRegDecl(const llc::Variable *reg) const;
  void visit(const llc::Call *call) const;
  void visit(const llc::Terminator *term) const;
  void visit(const llc::Place *place) const;
  void visit(const llc::PlaceBase *base) const;
  void visit(const llc::Projection &proj) const;
  void visit(const llc::Value *value) const;

  void indent() const;

  std::ostream &os;
  int tabsize;
};