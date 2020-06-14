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
#include "llvm/Analysis/DPP/TypeVisitor.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instructions.h"
#include <queue>

#define DEBUG_TYPE "DPPRule6"

using namespace llvm;
using namespace llvm::DPP;

const char DPPRule6L::RuleName[] = "DPPRule6L";
const char DPPRule6G::RuleName[] = "DPPRule6G";
AnalysisKey DPPRule6L::Key;
AnalysisKey DPPRule6G::Key;

namespace {

using BadLocalsMap = DPPRule6LResult::BadLocalsMap;

/// Define some common Rule6 Stuff here
struct TypeChecker : public TypeVisitor<TypeChecker> {
  bool FoundBuffer = false;
  bool FoundVulnerablePointer = false;
  bool visitPointerType(const PointerType *Ty);
  bool visitArrayType(const ArrayType *Ty);
  bool visitVectorType(const VectorType *Ty);
  void reset() {
    FoundBuffer = false;
    FoundVulnerablePointer = false;
  }
};

/// Visitor to look through all Alloca instruction
struct LocalsVisitor : public InstVisitor<LocalsVisitor> {
  BadLocalsMap *BadLocals;
  LocalsVisitor(BadLocalsMap *BadLocals) : BadLocals(BadLocals) {}
  void visitAllocaInst(AllocaInst &AI);
};

} // namespace

bool TypeChecker::visitPointerType(const PointerType *) {
  // Pointer is vulnerable if we've seen a previous buffer.
  LLVM_DEBUG(dbgs() << "TypeChecker: visitPointerType\n");
  FoundVulnerablePointer = FoundBuffer;
  // We can stop if we already found vulnerability.
  return FoundVulnerablePointer;
}
bool TypeChecker::visitArrayType(const ArrayType *) {
  // Assume that the buffer might corrupt its own elements.
  LLVM_DEBUG(dbgs() << "TypeChecker: visitArrayType\n");
  FoundBuffer = true;
  return FoundBuffer;
}
bool TypeChecker::visitVectorType(const VectorType *) {
  // Assume that the buffer might corrupt its own elements.
  LLVM_DEBUG(dbgs() << "TypeChecker: visitVectorType\n");
  FoundBuffer = true;
  return FoundBuffer;
}

void LocalsVisitor::visitAllocaInst(AllocaInst &AI) {
  TypeChecker Checker {};
  Checker.visit(AI.getAllocatedType());

  if (Checker.FoundVulnerablePointer) {
    BadLocals->try_emplace(&AI, "pointer in vulnerable structure");
  }
}

DPPRule6L::Result DPPRule6L::run(Function &F, AnalysisManager<Function> &AM) {
  Result Result {};

  LocalsVisitor Visitor(&Result.BadLocals);
  Visitor.visit(F);

  return Result;
}

DPPRule6G::Result DPPRule6G::run(Module &M, AnalysisManager<Module> &AM) {
  Result Result {};

  // Check if we got iffy global variables
  TypeChecker Checker {};
  for (auto &G : M.globals()) {
    if (G.isDeclaration())
      continue; // Skip declaration without definition
    if (G.isConstant())
      continue; // Skip constants

    LLVM_DEBUG(dbgs() << "DPPRule6G: inspecting GlobalValue of:\n");
    LLVM_DEBUG(G.getValueType()->dump());
    Checker.visit(G.getValueType());

    if (Checker.FoundVulnerablePointer) {
      Result.BadGlobals.try_emplace(&G, "pointer in vulnerable structure");
    }
    Checker.reset();
  }

  // Collect the local results into our Result object
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  for (auto &F : M) {
    if (F.isDeclaration())
      continue; // Skip declarations (i.e., functions without a definition)

    auto &R = FAM.getResult<DPPRule6L>(F);

    if (!R.empty()) // Store only non-empty results
      Result.FunctionInfo.try_emplace(&F, &R);
  }

  return Result;
}

raw_ostream &DPPRule6LResult::print(raw_ostream &OS) const {
  for (auto Bad : BadLocals)
    OS << *Bad.getFirst() << " (" << Bad.getSecond() << ")\n";
  return OS;
}

raw_ostream &DPPRule6GResult::print(raw_ostream &OS) const {
  OS << "Globals:\n";
  for (auto G : BadGlobals) {
    auto *const GV = G.getFirst();
    OS << "@" << GV->getGlobalIdentifier() << " = " << *GV->getValueType() <<
       " (" << G.getSecond() << ")\n";
  }

  OS << "Functions:\n";
  for (auto Func : FunctionInfo) {
    if (!Func.getSecond()->empty()) {
      OS << Func.getFirst()->getName() << ":\n";
      Func.getSecond()->print(OS);
    }
  }

  return OS;
}
