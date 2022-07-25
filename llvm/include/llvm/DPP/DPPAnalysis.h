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

struct ObjectInfo {
    const Value * Obj;
    int NumRulesFlagObj;
    int NumPointers;
};

class DPPAnalysisResult {
public:
  DenseSet<const Value *> FilteredInstructions;
public:
  DPPAnalysisResult() = delete;
  raw_ostream &print(raw_ostream &OS) const;
};

class DPPAnalysis : public AnalysisInfoMixin<DPPAnalysis> {
  friend AnalysisInfoMixin<DPPAnalysis>;
  static AnalysisKey Key;
public:
  using Result = DPPAnalysisResult;
  Result run(Module &M, AnalysisManager<Module> &AM);
  const SVF::VFGNode* getVFGNodeFromValue(SVF::PAG *pag, SVF::SVFG *svfg, const Value *val);
  ValSet GetCompleteUsers(const Value *Val, SVF::SVFG *svfg);
  ValSet getPointersToObject(const Value *Val, SVF::SVFG *svfg);
  std::pair<ValSet, uint32_t> getTotalDataPointers(SVF::SVFG *svfg);
  ValSet getDataPointersToObjects(DPPMap Map, SVF::SVFG *svfg);
  ValSet getDataPointersToObject2(const Value *Val, SVF::SVFG *svfg);
  ValSet getDataObjects(DPPMap Map);
  DPPMap filterObjects(DPPMap M1, DPPMap M2);
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
