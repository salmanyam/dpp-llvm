//==- DPPAnalysis.cpp ------------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DPP_DPPANALYSIS_H
#define LLVM_ANALYSIS_DPP_DPPANALYSIS_H

#include "llvm/DPP/DPP.h"
#include "llvm/DPP/DPPUtils.h"

namespace SVF {
    class PAG;
    class VFGNode;
    class SVFG;
} // namespace SVF

namespace llvm {
namespace DPP {

class DPPAnalysisResult {
private:
  StringRef Data;
public:
  DPPAnalysisResult() = delete;
  [[maybe_unused]] DPPAnalysisResult(StringRef Data) : Data(Data) {}

  raw_ostream &print(raw_ostream &OS) const {
    OS << Data;
    return OS;
  }
};

class DPPAnalysis : public AnalysisInfoMixin<DPPAnalysis> {
  friend AnalysisInfoMixin<DPPAnalysis>;
  static AnalysisKey Key;
public:
  using Result = DPPAnalysisResult;
  Result run(Module &M, AnalysisManager<Module> &AM);
  ValSet getPointersToObject(const Value *Val, SVF::SVFG *svfg);
  std::pair<ValSet, uint32_t> getTotalDataPointers(SVF::SVFG *svfg);
  ValSet getDataPointersToObjects(DPPMap Map, SVF::SVFG *svfg);
  ValSet getDataObjects(DPPMap Map);
};

class DPPAnalysisPrinterPass : public PassInfoMixin<DPPAnalysisPrinterPass> {
private:
  raw_ostream &OS;
public:
  explicit DPPAnalysisPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Module &M, AnalysisManager<Module> &AM);
};

} // namespace DPP
} // namespace llvm

#endif // LLVM_ANALYSIS_DPP_DPPANALYSIS_H
