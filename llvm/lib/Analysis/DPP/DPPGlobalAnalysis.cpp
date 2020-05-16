//==- DPPGlobalAnalysis.cpp ------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DPP/DPPGlobalAnalysis.h"
#include "llvm/Analysis/DPP/DPPLocalAnalysis.h"

#define DEBUG_TYPE "DPPGlobalAnalysis"

using namespace llvm;
using namespace DPP;

AnalysisKey DPPGlobalAnalysis::Key;

DPPGlobalAnalysis::Result
DPPGlobalAnalysis::run(Module &M, AnalysisManager<Module> &MAM) {

  LLVM_DEBUG(dbgs() << "DPPGlobalAnalysis::run starting up...\n");

  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  Result Result;

  for (auto &F : M)
    if (!F.isDeclaration()) // Skip undefined functions
      Result.emplace(&F, FAM.getResult<DPPLocalAnalysis>(F));

  return Result;
}


PreservedAnalyses
DPPGlobalPrinterPass::run(Module &M, AnalysisManager<Module> &AM) {
  OS << "Data Pointer Prioritization Global Analysis\n";
  for (auto &R : AM.getResult<DPPGlobalAnalysis>(M))
    OS << R.first->getName() << " - " << R.second << "\n";
  return PreservedAnalyses::all();
}
