//==- DPPRule6.h -----------------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DPP_DPPRULE6_H
#define LLVM_ANALYSIS_DPP_DPPRULE6_H

#include "llvm/DPP/DPPRule.h"
#include "llvm/DPP/DPPUtils.h"

#include "llvm/IR/PassManager.h"

namespace SVF {
    class PAG;
    class VFGNode;
    class SVFG;
} // namespace SVF

namespace llvm {
namespace DPP {

class DPPRule5LResult;
class DPPRule5GResult;

class DPPRule5L : public AnalysisInfoMixin<DPPRule5L> {
  friend AnalysisInfoMixin<DPPRule5L>;

public:
  using Result = DPPRule5LResult;

  static const char RuleName[];
  static AnalysisKey Key;

  Result run(Function &F, AnalysisManager<Function> &AM);
};

class DPPRule5LResult : public DPPResult<DPPRule5L> {
  friend DPPRule5L;
public:
  using BadLocalsMap = DenseMap<AllocaInst *, StringRef>;
public:
  BadLocalsMap BadLocals;
public:
  DPPRule5LResult() {}
  bool empty() const { return BadLocals.empty(); }
  raw_ostream &print(raw_ostream &OS) const;
};

class DPPRule5G : public AnalysisInfoMixin<DPPRule5G> {
  friend AnalysisInfoMixin<DPPRule5G>;
public:
  using Result = DPPRule5GResult;

  static const char RuleName[];
  static AnalysisKey Key;

  Result run(Module &M, AnalysisManager<Module> &AM);
  const SVF::VFGNode* getVFGNodeFromValue(SVF::PAG *pag, SVF::SVFG *svfg, const Value *val);
};

class DPPRule5GResult : public DPPResult<DPPRule5G> {
  friend DPPRule5G;
public:
  using BadGlobalsMap = DenseMap<GlobalValue *, StringRef>;
  using FunctionInfoMap= DenseMap<Function *, DPPRule5LResult *>;
public:
  BadGlobalsMap BadGlobals;
  FunctionInfoMap FunctionInfo;
  llvm::DPP::DPPMap PrioritizedPtrMap;
public:
  DPPRule5GResult() {}
  bool empty() { return BadGlobals.empty() && FunctionInfo.empty(); }
  raw_ostream &print(raw_ostream &OS) const;
};

class [[maybe_unused]] DPPRule5LPrinterPass
    : public DPPLocalPrinterPass<DPPRule5L> {
public:
  [[maybe_unused]] DPPRule5LPrinterPass(raw_ostream &OS)
      : DPPLocalPrinterPass(OS) {}
};

class [[maybe_unused]] DPPRule5GPrinterPass
    : public DPPGlobalPrinterPass<DPPRule5G> {
public:
  [[maybe_unused]] DPPRule5GPrinterPass(raw_ostream &OS)
      : DPPGlobalPrinterPass(OS) {}
};

} // namespace DPP
} // namespace llvm

#endif // LLVM_ANALYSIS_DPP_DPPRULE6_H
