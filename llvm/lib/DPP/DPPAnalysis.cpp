//==- DPPGlobalAnalysis.cpp ------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/DPP/DPPAnalysis.h"

#define DEBUG_TYPE "DPPGlobalAnalysis"

using namespace llvm;
using namespace llvm::DPP;

AnalysisKey DPPAnalysis::Key;

DPPAnalysis::Result DPPAnalysis::run(Module &M,
                                                 AnalysisManager<Module> &AM) {
  LLVM_DEBUG(dbgs() << "DPPGlobalAnalysis::run starting up...\n");
  return DPPAnalysis::Result("not implemented");
}

PreservedAnalyses DPPAnalysisPrinterPass::run(Module &M, AnalysisManager<Module> &AM) {
  OS << "Data Pointer Prioritization Analysis\n";

  auto Results = AM.getResult<DPPAnalysis>(M);
  Results.print(OS);

  return PreservedAnalyses::all();
}
