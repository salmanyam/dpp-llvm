//==- DPPLocalAnalysis.cpp -------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DPP/DPP.h"
#include "llvm/Analysis/DPP/DPPLocalAnalysis.h"
#include "llvm/IR/Instructions.h"

#define DEBUG_TYPE "DPPLocalAnalysis"

using namespace llvm;
using namespace DPP;

AnalysisKey DPPLocalAnalysis::Key;

DPPLocalAnalysis::Result DPPLocalAnalysis::run(Function &F,
                                               AnalysisManager<Function> &AM) {
  LLVM_DEBUG(dbgs() << "DPPLocalAnalysis::run entered\n");
  Result Results;

  // Add new rules here
  DPPLocalRule *Rules[]{
      createLocalRule6(this)
  };

  for (auto *Rule : Rules) {
    auto Result = Rule->runOnFunction(F, AM);
    Results.try_emplace(Result->getType(), Result);
    delete Rule;
  }

  return Results;
}

PreservedAnalyses
DPPLocalPrinterPass::run(Function &F, AnalysisManager<Function> &AM) {
  OS << "Data Pointer Prioritization Local Analysis\n";
  OS << F.getName() << ":\n";
  auto Results = AM.getResult<DPPLocalAnalysis>(F);
  for (auto &Result : Results) {
    OS << Result.getSecond();
  }
  return PreservedAnalyses::all();
}

