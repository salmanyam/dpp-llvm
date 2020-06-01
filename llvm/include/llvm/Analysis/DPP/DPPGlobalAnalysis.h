//
// Created by ishkamiel on 09/06/2020.
//

#ifndef LLVM_DPPGLOBALANALYSIS_H
#define LLVM_DPPGLOBALANALYSIS_H

#include "llvm/Analysis/DPP/DPP.h"

namespace llvm {
namespace DPP {

class DPPGlobalResult {
private:
  StringRef data;
public:
  DPPGlobalResult() = delete;
  DPPGlobalResult(StringRef data) : data(data) {}

  raw_ostream &print(raw_ostream &OS) const {
    OS << data;
    return OS;
  }
};

class DPPGlobalAnalysis : public AnalysisInfoMixin<DPPGlobalAnalysis> {
  friend AnalysisInfoMixin<DPPGlobalAnalysis>;
  static AnalysisKey Key;
public:
  using Result = DPPGlobalResult;
  Result run(Module &M, AnalysisManager<Module> &AM);
};

class DPPGlobalPrinterPass : public PassInfoMixin<DPPGlobalPrinterPass> {
private:
  raw_ostream &OS;
public:
  explicit DPPGlobalPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Module &M, AnalysisManager<Module> &AM);
};

} // namespace DPP
} // namespace llvm

#endif // LLVM_DPPGLOBALANALYSIS_H
