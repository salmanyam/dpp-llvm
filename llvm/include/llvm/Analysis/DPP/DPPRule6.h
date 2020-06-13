//
// Created by ishkamiel on 09/06/2020.
//

#ifndef LLVM_DPPRULE6_H
#define LLVM_DPPRULE6_H

#include "llvm/Analysis/DPP/DPPRule.h"
#include "llvm/Analysis/DPP/TypeVisitor.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
namespace DPP {

class DPPRule6LResult;

class DPPRule6L : public AnalysisInfoMixin<DPPRule6L> {
  friend AnalysisInfoMixin<DPPRule6L>;

public:
  using Result = DPPRule6LResult;

  static const char RuleName[];
  static AnalysisKey Key;

  Result run(Function &F, AnalysisManager<Function> &AM);
};


class DPPRule6LResult : public DPPResult<DPPRule6L> {
  friend DPPRule6L;
public:
  using BadLocalsMap = DenseMap<AllocaInst *, StringRef>;

private:
  BadLocalsMap BadLocals;

public:
  DPPRule6LResult() {}

  raw_ostream &print(raw_ostream &OS) const {
    for (auto Bad : BadLocals) {
      OS << *Bad.getFirst() << " (" << Bad.getSecond() << ")\n";
    }
    return OS;
  }
};

class DPPRule6LPrinterPass : public DPPLocalPrinterPass<DPPRule6L> {
public:
  DPPRule6LPrinterPass(raw_ostream &OS) : DPPLocalPrinterPass(OS) {}
};

} // namespace DPP
} // namespace llvm

#endif // LLVM_DPPRULE6_H
