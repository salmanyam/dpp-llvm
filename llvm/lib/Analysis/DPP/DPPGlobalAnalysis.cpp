//==- DPPGlobalAnalysis.cpp ------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DPP/DPP.h"
#include "llvm/Analysis/DPP/DPPGlobalAnalysis.h"

#define DEBUG_TYPE "DPPGlobalAnalysis"

using namespace llvm;
using namespace DPP;

AnalysisKey DPPGlobalAnalysis::Key;

DPPGlobalAnalysis::Result
DPPGlobalAnalysis::run(Module &M, AnalysisManager<Module> &MAM) {

  LLVM_DEBUG(dbgs() << "DPPGlobalAnalysis::run starting up...\n");

  Result Results;

  // Add new rules here
  DPPGlobalRule *Rules[]{
      createGlobalRule6(this)
  };

  for (auto *Rule : Rules) {
    auto Result = Rule->runOnModule(M, MAM);
    Results.try_emplace(Result->getType(), Result);
    delete Rule;
  }

  return Results;
}


PreservedAnalyses
DPPGlobalPrinterPass::run(Module &M, AnalysisManager<Module> &AM) {
  OS << "Data Pointer Prioritization Global Analysis\n";
  auto Results = AM.getResult<DPPGlobalAnalysis>(M);
  for (auto &Result : Results) {
    OS << Result.getSecond();
  }
  return PreservedAnalyses::all();
}
