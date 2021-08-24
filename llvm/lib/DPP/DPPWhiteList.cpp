//==- DPPWhiteList.cpp -----------------------------------------------------==//
//
// Under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/StackSafetyAnalysis.h"
#include "llvm/DPP/DPPWhiteList.h"
#include "llvm/DPP/TypeVisitor.h"
#include "llvm/IR/InstVisitor.h"

#define DEBUG_TYPE "DPPWhiteList"

using namespace llvm;
using namespace llvm::DPP;

[[maybe_unused]] const char DPPWhiteList::RuleName[] = "DPPWhiteList";
AnalysisKey DPPWhiteList::Key;

DPPWhiteList::Result DPPWhiteList::run(Module &M, AnalysisManager<Module> &AM) {
  return Result{AM.getResult<StackSafetyGlobalAnalysis>(M)};
}

bool DPPWhiteListResult::isSafe(const Value* V) const {
  if (const auto* AI = dyn_cast<AllocaInst>(V)) {
    // Check if SSG can prove safety of AllocaInst
    if (SSGI->isSafe(*AI)) {
        return true;
    }
  }

  // Default is unsafe if nothing can prove otherwise
  return false; 
}

PreservedAnalyses DPPWhiteListPrinterPass::run(Module &M,
                                               AnalysisManager<Module> &AM) {
  auto Result = AM.getResult<DPPWhiteList>(M);
  OS << Result.getName() << " isSafe:\n";


  for (auto &F : M) {
    // Skip funcs without definition
    if (F.isDeclaration())
      continue;

    OS << "  @" << F.getName() << (F.isDSOLocal() ? "" : " dso_preemptable")
       << (F.isInterposable() ? " interposable" : "") << "\n";

    for (auto &BB : F) {
      for (auto &I : BB) {
        if (Result.isSafe(&I)) {
          OS << "    " << I << "\n";
        }
      }
    }

  }

  return PreservedAnalyses::all();
}
