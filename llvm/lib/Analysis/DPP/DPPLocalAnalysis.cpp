//==- DPPLocalAnalysis.cpp -------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DPP/DPPLocalAnalysis.h"

#define DEBUG_TYPE "DPPLocalAnalysis"

using namespace llvm;
using namespace DPP;

AnalysisKey DPPLocalAnalysis::Key;
//char DPPLocalAnalysisWrapper::ID = 0;

DPPLocalAnalysis::Result DPPLocalAnalysis::run(Function &F,
                                               AnalysisManager<Function> &AM) {
  LLVM_DEBUG(dbgs() << "DPPLocalAnalysis::run entered\n");
  // Dependencies:
  // AM.getResult<ScalarEvolutionAnalysis>(F);
  return runOnFunction(F);
}

DPPLocalAnalysis::Result DPPLocalAnalysis::runOnFunction(Function &F) {
  return DEBUG_TYPE " not implemented";
}

PreservedAnalyses
DPPLocalPrinterPass::run(Function &F, AnalysisManager<Function> &AM) {
  OS << "Data Pointer Prioritization Local Analysis\n";
  OS << F.getName() << " - " << AM.getResult<DPPLocalAnalysis>(F) << "\n";
  return PreservedAnalyses::all();
}

