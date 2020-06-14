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
AnalysisKey DPPRule6L::Key;

namespace {

using BadLocalsMap = DPPRule6LResult::BadLocalsMap;

/// Define some common Rule6 Stuff here
struct TypeChecker : public TypeVisitor<TypeChecker> {
  bool FoundBuffer = false;
  bool FoundVulnerablePointer = false;
  bool visitPointerType(const PointerType *Ty);
  bool visitArrayType(const ArrayType *Ty);
  bool visitVectorType(const VectorType *Ty);
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
  FoundVulnerablePointer = FoundBuffer;
  // We can stop if we already found vulnerability.
  return FoundVulnerablePointer;
}
bool TypeChecker::visitArrayType(const ArrayType *) {
  // Assume that the buffer might corrupt its own elements.
  FoundBuffer = true;
  return FoundBuffer;
}
bool TypeChecker::visitVectorType(const VectorType *) {
  // Assume that the buffer might corrupt its own elements.
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

raw_ostream &DPPRule6LResult::print(raw_ostream &OS) const {
  for (auto Bad : BadLocals)
    OS << *Bad.getFirst() << " (" << Bad.getSecond() << ")\n";
  return OS;
}
