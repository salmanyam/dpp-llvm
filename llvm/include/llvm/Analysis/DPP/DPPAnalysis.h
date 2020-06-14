//==- DPPAnalysis.cpp ------------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DPP_DPPGLOBALANALYSIS_H
#define LLVM_ANALYSIS_DPP_DPPGLOBALANALYSIS_H

#include "llvm/Analysis/DPP/DPP.h"

namespace llvm {
namespace DPP {

class DPPAnalysisResult {
private:
  StringRef data;
public:
  DPPAnalysisResult() = delete;
  DPPAnalysisResult(StringRef data) : data(data) {}

  raw_ostream &print(raw_ostream &OS) const {
    OS << data;
    return OS;
  }
};

class DPPAnalysis : public AnalysisInfoMixin<DPPAnalysis> {
  friend AnalysisInfoMixin<DPPAnalysis>;
  static AnalysisKey Key;
public:
  using Result = DPPAnalysisResult;
  Result run(Module &M, AnalysisManager<Module> &AM);
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

#endif // LLVM_ANALYSIS_DPP_DPPGLOBALANALYSIS_H
