//
// Created by salman on 6/29/21.
//

#include "llvm/DPP/DPPInstr.h"
#include "llvm/DPP/DPPUtils.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constant.h"

using namespace llvm;
using namespace llvm::DPP;

#define DEBUG_TYPE "DPPInstrument"

STATISTIC(StatSignStoreData, "data pointer stores instrumented");
STATISTIC(StatAuthLoadData, "data pointer loads instrumented");

PreservedAnalyses DPPInstr::run(Function &F, AnalysisManager<Function> &AM) {
    //todo: where, when, and why do we need this attribute?
    if (F.hasFnAttribute("no-parts"))
        return PreservedAnalyses::all();
    if (! DPP::useDpi())
        return PreservedAnalyses::all();

    bool function_modified = false;

    for (auto &BB:F) {
        for (auto &I: BB) {
            function_modified = handleInstruction(F, I) || function_modified;
        }
    }

    return function_modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

inline bool DPPInstr::handleInstruction(Function &F, Instruction &I) {
    bool modified = false;
    switch(I.getOpcode()) {
        default:
            break;
        case Instruction::Store:
            modified = handleStoreInstruction(F, dyn_cast<StoreInst>(&I));
            break;
        case Instruction::Load:
            modified = handleLoadInstruction(F, dyn_cast<LoadInst>(&I));
            break;
    }

    return modified;
}

bool DPPInstr::handleStoreInstruction(Function &F, StoreInst *pSI) {
    const auto V = pSI->getValueOperand();
    const auto VType = V->getType();

    if (! isDataPointer(VType))
        return false;

    pSI->setOperand(0, createPartsIntrinsic(F, *pSI, V, Intrinsic::pa_pacda));

    ++StatSignStoreData;
    return true;
}

bool DPPInstr::handleLoadInstruction(Function &F, LoadInst *pLI) {
    const auto VType = pLI->getPointerOperandType()->getPointerElementType();

    if (! isDataPointer(VType))
        return false;

    CallInst *authenticated = nullptr;

    if (isUnionTypePunningSupported() && isUnionMemberLoad(pLI))
        authenticated = createPartsIntrinsicNoModifier(F, *pLI->getNextNode(), pLI, Intrinsic::pa_xpacd);
    else
        authenticated = createPartsIntrinsic(F, *pLI->getNextNode(), pLI, Intrinsic::pa_autda);
    assert(authenticated != nullptr);

    pLI->replaceAllUsesWith(authenticated);
    authenticated->setOperand(0, pLI);

    ++StatAuthLoadData;
    return true;
}

Constant *DPPInstr::getZeroConstant(LLVMContext &C) {
    static auto *zero = Constant::getIntegerValue(Type::getInt64Ty(C),
                                                  APInt(64, 0));
    return zero;
}

CallInst *DPPInstr::createPartsIntrinsic(Function &F,
                                       Instruction &I,
                                       Value *calledValue,
                                       Intrinsic::ID intrinsicID) {
    const auto calledValueType = calledValue->getType();

    // Generate Builder for inserting PARTS intrinsic
    IRBuilder<> Builder(&I);
    // Get PARTS intrinsic declaration for correct input type
    auto autcall = Intrinsic::getDeclaration(F.getParent(), intrinsicID, {calledValueType});

    // Insert PARTS intrinsics
    Constant *Zero = getZeroConstant(F.getContext());
    auto paced = Builder.CreateCall(autcall, {calledValue, Zero}, "");

    return paced;
}

CallInst *DPPInstr::createPartsIntrinsicNoModifier(Function &F,
                                         Instruction &I,
                                         Value *calledValue,
                                         Intrinsic::ID intrinsicID) {
    const auto calledValueType = calledValue->getType();

    // Generate Builder for inserting PARTS intrinsic
    IRBuilder<> Builder(&I);
    // Get PARTS intrinsic declaration for correct input type
    auto autcall = Intrinsic::getDeclaration(F.getParent(), intrinsicID, {calledValueType});

    // Insert PARTS intrinsics
    auto paced = Builder.CreateCall(autcall, {calledValue}, "");

    return paced;
}

bool DPPInstr::isUnionMemberLoad(LoadInst *load) {
    auto defVal = load->getOperandUse(load->getPointerOperandIndex()).get();
    if (!isa<BitCastInst>(defVal) && !isa<BitCastOperator>(defVal))
        return false;

    Type *bcSrcType;
    if (isa<BitCastInst>(defVal))
        bcSrcType = dyn_cast<BitCastInst>(defVal)->getSrcTy();
    else
        bcSrcType = dyn_cast<BitCastOperator>(defVal)->getSrcTy();

    if (!isa<PointerType>(bcSrcType))
        return false;

    auto bcSrcPointerType = dyn_cast<PointerType>(bcSrcType);
    if (!bcSrcPointerType->getElementType()->isStructTy())
        return false;

    auto unionType = dyn_cast<StructType>(bcSrcPointerType->getElementType());
    if (!unionType->getName().startswith("union."))
        return false;

    return true;
}
