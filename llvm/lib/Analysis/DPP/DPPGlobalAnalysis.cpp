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
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "DPPGlobalAnalysis"

using namespace llvm;
using namespace llvm::DPP;

AnalysisKey DPPGlobalAnalysis::Key;

DPPGlobalAnalysis::Result DPPGlobalAnalysis::run(Module &M,
                                                 AnalysisManager<Module> &AM) {
  LLVM_DEBUG(dbgs() << "DPPGlobalAnalysis::run starting up...\n");
  return DPPGlobalAnalysis::Result("not implemented");
}

PreservedAnalyses
DPPGlobalPrinterPass::run(Module &M, AnalysisManager<Module> &AM) {
  OS << "Data Pointer Prioritization Analysis\n";

  auto Results = AM.getResult<DPPGlobalAnalysis>(M);
  Results.print(OS);

  return PreservedAnalyses::all();
}
