//==- DPPLocalAnalysis.h ---------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DPP_DPPLOCALANALYSIS_H
#define LLVM_ANALYSIS_DPP_DPPLOCALANALYSIS_H

#include "llvm/Analysis/DPP/DPP.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

namespace DPP {

class DPPLocalAnalysis : public AnalysisInfoMixin<DPPLocalAnalysis> {
  friend AnalysisInfoMixin<DPPLocalAnalysis>;

public:
  using Result = DPPResultMap;
  static AnalysisKey Key;

  Result run(Function &F, AnalysisManager<Function> &AM);

private:
};

class DPPLocalPrinterPass : public PassInfoMixin<DPPLocalPrinterPass> {
private:
  raw_ostream &OS;

public:
  explicit DPPLocalPrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Function &M, AnalysisManager<Function> &AM);
};

} // namespace DPP

#endif // LLVM_ANALYSIS_DPP_DPPLOCALANALYSIS_H
