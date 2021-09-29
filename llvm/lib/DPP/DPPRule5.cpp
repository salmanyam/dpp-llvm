//==- DPPRule5.cpp ---------------------------------------------------------==//
//
// Under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Looks for allocas or global values that use a potentially problematic type.
// This is split into a Function pass that finds and checks allocas within
// functions, and a Module pass that combines the local information and goes
// through all GlobalValues.
//===----------------------------------------------------------------------===//

#include "llvm/DPP/SVFInitPass.h"
#include "llvm/DPP/DPPRule5.h"
#include "llvm/DPP/TypeVisitor.h"
#include "llvm/IR/InstVisitor.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

#define DEBUG_TYPE "DPPRule5"

using namespace llvm;
using namespace llvm::DPP;
using namespace SVF;

[[maybe_unused]] const char DPPRule5L::RuleName[] = "DPPRule5L";
[[maybe_unused]] const char DPPRule5G::RuleName[] = "DPPRule5G";
AnalysisKey DPPRule5L::Key;
AnalysisKey DPPRule5G::Key;

namespace {

using BadLocalsMap = DPPRule5LResult::BadLocalsMap;

/// Define some common Rule5 Stuff here
struct TypeChecker : public TypeVisitor<TypeChecker> {
    bool FoundBuffer = false;
    bool FoundVulnerablePointer = false;

    [[maybe_unused]] bool visitPointerType(const PointerType *Ty);
    [[maybe_unused]] bool visitArrayType(const ArrayType *Ty);
    [[maybe_unused]] bool visitVectorType(const VectorType *Ty);

    void reset() {
        FoundBuffer = false;
        FoundVulnerablePointer = false;
    }
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

namespace {

/// Visitor to look through all Alloca instruction
struct LocalsVisitor : public InstVisitor<LocalsVisitor> {
    BadLocalsMap *BadLocals;

    LocalsVisitor(BadLocalsMap *BadLocals) : BadLocals(BadLocals) {}
    void visitAllocaInst(AllocaInst &AI);
};

} // namespace

void LocalsVisitor::visitAllocaInst(AllocaInst &AI) {
    TypeChecker Checker{};
    Checker.visit(AI.getAllocatedType());

    if (Checker.FoundVulnerablePointer) {
        BadLocals->try_emplace(&AI, "pointer in vulnerable structure");
    }
}

DPPRule5L::Result DPPRule5L::run(Function &F, AnalysisManager<Function> &AM) {
    Result Result{};

    LocalsVisitor Visitor(&Result.BadLocals);
    Visitor.visit(F);

    return Result;
}

const VFGNode* DPPRule5G::getVFGNodeFromValue(PAG *pag, SVFG *svfg, const Value *val) {
    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(val));
    const VFGNode* vNode = svfg->getDefSVFGNode(pNode);
    return vNode;
}

DPPRule5G::Result DPPRule5G::run(Module &M, AnalysisManager<Module> &AM) {
    Result Result{};

    auto R = AM.getResult<SVFInitPass>(M);

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *CallGraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    // Check if we got iffy global variables
    TypeChecker Checker{};
    for (auto &G : M.globals()) {
        if (G.isConstant())
            continue; // Skip constants

        LLVM_DEBUG(dbgs() << "DPPRule5G: inspecting GlobalValue of type: ");
        LLVM_DEBUG(G.getValueType()->dump());

        Checker.visit(G.getValueType());

        if (Checker.FoundVulnerablePointer) {
            Result.BadGlobals.try_emplace(&G, "pointer in vulnerable structure");

            if (DPP::isDataPointer(G.getType()->getPointerElementType())) {
                Result.PrioritizedPtrMap.try_emplace(&G, 1);
            }
        }
        Checker.reset();
    }

    /// write some logs to file
    string dppLog = "#################### RULE 5 #########################\n";

    // Collect the local results into our Result object
    auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
    for (auto &F : M) {
        if (F.isDeclaration())
            continue; // Skip declarations (i.e., functions without a definition)

        auto &R = FAM.getResult<DPPRule5L>(F);

        if (!R.empty()) {// Store only non-empty results
            Result.FunctionInfo.try_emplace(&F, &R);
            for (auto V : R.BadLocals) {
                Result.PrioritizedPtrMap.try_emplace(V.getFirst(), 1);

                auto SVFNode = getVFGNodeFromValue(pag, svfg, V.getFirst());
                dppLog += SVFNode->toString() + "\n";
                dppLog += "--------------------------------------------------------------\n";
            }
        }
    }

    dppLog += "##################################################\n\n\n";
    DPP::writeDPPLogsToFile(dppLog);

    return Result;
}

raw_ostream &DPPRule5LResult::print(raw_ostream &OS) const {
    for (auto Bad : BadLocals)
        OS << *Bad.getFirst() << " (" << Bad.getSecond() << ")\n";
    return OS;
}

raw_ostream &DPPRule5GResult::print(raw_ostream &OS) const {
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
