//
// Created by salman on 6/29/21.
//

#include "llvm/DPP/DPPInstr.h"
#include "llvm/DPP/DPPUtils.h"
#include "llvm/DPP/DPPAnalysis.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constant.h"


using namespace llvm;
using namespace llvm::DPP;

#define DEBUG_TYPE "DPPInstrument"

STATISTIC(StatSignStoreData, "data pointer stores instrumented");
STATISTIC(StatAuthLoadData, "data pointer loads instrumented");

int LoadInstructions = 0;
int StoreInstructions = 0;

PreservedAnalyses DPPInstr::run(Module &M, AnalysisManager<Module> &AM) {

    ValSet DataPointers;
    //DenseSet<const Value *> FilteredInstructions;
    if (DPP::useDpi()) {

        auto R = AM.getResult<SVFInitPass>(M);
        SVFG *svfg = R.SVFParams.svfg;

        auto DPPResult = AM.getResult<DPP::DPPAnalysis>(M);
        //FilteredInstructions = DPPResult.FilteredInstructions;
        for (auto DPVal: DPPResult.FilteredInstructions) {
            //auto Pointers = getPointersToObject(DPVal, svfg);
            auto Pointers = GetCompleteUsers(DPVal, svfg);
            for (auto Item: Pointers) {
                if (const auto *I = dyn_cast<Instruction>(Item)) {
                    if (I->getOpcode() == Instruction::Load || I->getOpcode() == Instruction::Store) {
                        DataPointers.insert(Item);

                        /*for (const auto U : I->users()) {
                            if (const auto *I2 = dyn_cast<Instruction>(U)) {
                                if (I2->getOpcode() == Instruction::Load || I->getOpcode() == Instruction::Store) {
                                    DataPointers.insert(I2);
                                }
                            }
                        }*/
                    }
                }
            }

            Pointers = getPointersToObject(DPVal, svfg);
            for (auto Item: Pointers) {
                if (const auto *I = dyn_cast<Instruction>(Item)) {
                    if (I->getOpcode() == Instruction::Load || I->getOpcode() == Instruction::Store) {
                        DataPointers.insert(Item);
                    }
                }
            }
        }
    }

    bool function_modified = false;

    for (auto &F : M) {
        //todo: where, when, and why do we need this attribute?
        if (F.hasFnAttribute("no-parts")) {
            function_modified = false;
            continue;
        }

        for (auto &BB:F) {
            for (auto &I: BB) {
                if (DPP::useDpi()) {
                    if (DataPointers.find(&I) != DataPointers.end()) {
                        //errs() << "Found Instruction = " << I << "\n";
                        function_modified = handleInstruction(F, I) || function_modified;
                    }
                } else {
                    function_modified = handleInstruction(F, I) || function_modified;
                }
            }
        }
    }

    errs() << LoadInstructions << " " << StoreInstructions << "\n";

    return function_modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

ValSet DPPInstr::GetCompleteUsers(const Value *Val, SVFG *svfg) {
    FIFOWorkList<const Value *> worklist;
    ValSet visited;

    /// insert the Val in the worklist as we need to check its existence as well
    worklist.push(Val);

    /// get all the pointers pointing to an object, i.e., the object pointed by Val
    /*auto Pointers = getPointersToObject(Val, svfg);
    for (auto Ptr: Pointers) {
        if (visited.find(Ptr) == visited.end())
            worklist.push(Ptr);
    }*/

    /// Traverse along all uses of Val
    while (!worklist.empty()) {
        const Value *V = worklist.pop();
        //errs() << "POPPING: " << *V << "\n";
        visited.insert(V);

        for (const auto U : V->users()) {
            //errs() << *U << "\n";
            if (visited.find(U) == visited.end())
                worklist.push(U);
        }
    }

    return visited;
}

/// get all the pointers pointing to the object pointed by Val
ValSet DPPInstr::getPointersToObject(const Value *Val, SVFG *svfg) {
    ValSet Pointers;
    NodeID pNodeId = svfg->getPAG()->getObjectNode(Val);
    const NodeBS& pts = svfg->getPTA()->getRevPts(pNodeId);
    for (unsigned int pt : pts)
    {
        if (!svfg->getPAG()->hasGNode(pt))
            continue;

        PAGNode* targetPtr = svfg->getPAG()->getPAGNode(pt);
        if(targetPtr->hasValue())
        {
            //errs() << *targetPtr->getValue() << "\n";
            Pointers.insert(targetPtr->getValue());
        }
    }
    return Pointers;
}

inline bool DPPInstr::handleInstruction(Function &F, Instruction &I) {
    bool modified = false;
    switch(I.getOpcode()) {
        default:
            break;
        case Instruction::Store:
            //errs() << "Store: " << I << "\n";
            modified = handleStoreInstruction(F, dyn_cast<StoreInst>(&I));
            break;
        case Instruction::Load:
            //errs() << "Load: " << I << "\n";
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
    //errs() << *pSI << "\n";
    pSI->setOperand(0, createPartsIntrinsic(F, *pSI, V, Intrinsic::pa_pacda));

    ++StatSignStoreData;
    StoreInstructions++;

    return true;
}

bool DPPInstr::handleLoadInstruction(Function &F, LoadInst *pLI) {
    const auto VType = pLI->getPointerOperandType()->getPointerElementType();

    if (! isDataPointer(VType))
        return false;

    //errs() << *pLI << "\n";
    CallInst *authenticated = nullptr;

    if (isUnionTypePunningSupported() && isUnionMemberLoad(pLI))
        authenticated = createPartsIntrinsicNoModifier(F, *pLI->getNextNode(), pLI, Intrinsic::pa_xpacd);
    else
        authenticated = createPartsIntrinsic(F, *pLI->getNextNode(), pLI, Intrinsic::pa_autda);
    assert(authenticated != nullptr);

    pLI->replaceAllUsesWith(authenticated);
    authenticated->setOperand(0, pLI);

    ++StatAuthLoadData;
    LoadInstructions++;

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
