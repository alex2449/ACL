#pragma once

#include <iostream>
#include <unordered_map>

#include "cfg.hpp"

struct Function;

class CFGPrinter {
 public:
  CFGPrinter(std::ostream &os, int tabsize) : os(os), tabsize(tabsize) { }

  void visit(const std::vector<Function *> &funcs, const std::unordered_map<const Function *, cfg::CFG> &cfg_map) const;

 private:
  void printFunctionName(const Function *func) const;
  void visit(const Function *func, const cfg::CFG &cfg) const;
  void visit(const cfg::BasicBlock *block) const;
  void printBlockInfo(const cfg::BasicBlock *block) const;
  void printBlockSet(const std::vector<cfg::BasicBlock *> &blocks) const;
  void visit(const cfg::Instruction *inst) const;
  void visit(const cfg::Call *call) const;
  void visit(const cfg::Terminator *term) const;
  void visit(const cfg::Place *place) const;
  void visit(const cfg::PlaceBase *base) const;
  void visit(const cfg::Projection &proj) const;
  void visit(const cfg::Value *value) const;

  void indent() const;

  std::ostream &os;
  int tabsize;
};
