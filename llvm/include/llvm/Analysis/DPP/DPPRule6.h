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

#include "llvm/Analysis/DPP/DPPRule.h"
#include "llvm/Analysis/DPP/TypeVisitor.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
namespace DPP {

class DPPRule6LResult;
class DPPRule6GResult;

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
  bool empty() const { return BadLocals.empty(); }
  raw_ostream &print(raw_ostream &OS) const;
};

class DPPRule6G : public AnalysisInfoMixin<DPPRule6G> {
  friend AnalysisInfoMixin<DPPRule6G>;
public:
  using Result = DPPRule6GResult;

  static const char RuleName[];
  static AnalysisKey Key;

  Result run(Module &M, AnalysisManager<Module> &AM);
};

class DPPRule6GResult : public DPPResult<DPPRule6G> {
  friend DPPRule6G;
public:
  using BadGlobalsMap = DenseMap<GlobalValue *, StringRef>;
  using FunctionInfoMap= DenseMap<Function *, DPPRule6LResult *>;
private:
  BadGlobalsMap BadGlobals;
  FunctionInfoMap FunctionInfo;
public:
  DPPRule6GResult() {}
  bool empty() { return BadGlobals.empty() && FunctionInfo.empty(); }
  raw_ostream &print(raw_ostream &OS) const;
};

class DPPRule6LPrinterPass : public DPPLocalPrinterPass<DPPRule6L> {
public:
  DPPRule6LPrinterPass(raw_ostream &OS) : DPPLocalPrinterPass(OS) {}
};

class DPPRule6GPrinterPass : public DPPGlobalPrinterPass<DPPRule6G> {
public:
  DPPRule6GPrinterPass(raw_ostream &OS) : DPPGlobalPrinterPass(OS) {}
};

} // namespace DPP
} // namespace llvm

#endif // LLVM_ANALYSIS_DPP_DPPRULE6_H
