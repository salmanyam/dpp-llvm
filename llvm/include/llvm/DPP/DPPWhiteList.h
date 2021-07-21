//==- DPPWhiteList.h -------------------------------------------------------==//
//
// Under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Collects other analyses that prove safety properties of pointers or allocs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DPP_DPPWHITELIST_H
#define LLVM_ANALYSIS_DPP_DPPWHITELIST_H

#include "llvm/DPP/DPPRule.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class StackSafetyGlobalInfo;

namespace DPP {

class DPPWhiteListResult;

class DPPWhiteList : public AnalysisInfoMixin<DPPWhiteList> {
  friend AnalysisInfoMixin<DPPWhiteList>;

public:
  using Result = DPPWhiteListResult;

  static const char RuleName[];
  static AnalysisKey Key;

  Result run(Module &M, AnalysisManager<Module> &AM);
};

class DPPWhiteListResult : public DPPResult<DPPWhiteList> {
  friend DPPWhiteList;
private:
  StackSafetyGlobalInfo* SSGI;
public:
  DPPWhiteListResult(StackSafetyGlobalInfo& SSGI) : SSGI(&SSGI) {}
  /**
   * @brief Checks whether given Value is safe
   * 
   * Returns true if given Value is memory safe, as defined by Kutznetsov et al.
   * and used by StackSafetyAnalysis. That is, an allocation is memory safe, if
   * all pointers based on the allocation are guaranteed to only touch the
   * alloction. In other words, if all pointers derived from the initial pointer
   * to the allocation are guaranteed to not overflow, underflow, cause
   * use-after-frees or other errors.
   * 
   * This is currently only a wrapper around the StackSafetyAnalysis and only
   * analyzes AllocInst Values, defaulting to false for any others.
   */
  bool isSafe(const Value*) const;
};

class [[maybe_unused]] DPPWhiteListPrinterPass
    : public DPPGlobalPrinterPass<DPPWhiteList> {
public:
  [[maybe_unused]] DPPWhiteListPrinterPass(raw_ostream &OS)
      : DPPGlobalPrinterPass(OS) {}
      
  PreservedAnalyses run(Module &M, AnalysisManager<Module> &AM); 
};

} // namespace DPP
} // namespace llvm

#endif // LLVM_ANALYSIS_DPP_DPPWHITELIST_H
