//==- DPPRule6.cpp ---------------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DPP/DPPRule6.h"
#include "llvm/IR/Instructions.h"

#define DEBUG_TYPE "DPPRule6"

using namespace llvm;
using namespace llvm::DPP;

const char DPPRule6L::RuleName[] = "DPPRule6L";
AnalysisKey DPPRule6L::Key;

DPPRule6L::Result DPPRule6L::run(Function &F, AnalysisManager<Function> &AM) {
  return DPPRule6L::Result("not implemented");
}

