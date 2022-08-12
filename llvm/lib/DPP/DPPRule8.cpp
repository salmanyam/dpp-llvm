//
// Created by salman on 7/13/21.
//

#include "llvm/DPP/SVFInitPass.h"
#include "llvm/DPP/DPPRule1.h"
#include "llvm/DPP/DPPRule8.h"
#include "llvm/DPP/DPPWhiteList.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include "llvm/Analysis/TargetLibraryInfo.h"


#define DEBUG_TYPE "DPPRule8"

using namespace llvm;
using namespace llvm::DPP;
using namespace SVF;


[[maybe_unused]] const char DPPRule8L::RuleName[] = "DPPRule8L";
[[maybe_unused]] const char DPPRule8G::RuleName[] = "DPPRule8G";
AnalysisKey DPPRule8L::Key;
AnalysisKey DPPRule8G::Key;


// This is a rough heuristic; it may cause both false positives and
// false negatives. The proper implementation requires cooperation with
// the frontend.
bool DPPRule8L::isInterestingPointerComparison(Instruction *I) {
    if (ICmpInst *Cmp = dyn_cast<ICmpInst>(I)) {
        if (!Cmp->isRelational())
            return false;
    } else {
        return false;
    }
    return isPointerOperand(I->getOperand(0)) &&
    isPointerOperand(I->getOperand(1));
}

// This is a rough heuristic; it may cause both false positives and
// false negatives. The proper implementation requires cooperation with
// the frontend.
bool DPPRule8L::isInterestingPointerSubtraction(Instruction *I) {
    if (BinaryOperator *BO = dyn_cast<BinaryOperator>(I)) {
        if (BO->getOpcode() != Instruction::Sub)
            return false;
    } else {
        return false;
    }
    return isPointerOperand(I->getOperand(0)) &&
    isPointerOperand(I->getOperand(1));
}

uint64_t DPPRule8L::getAllocaSizeInBytes(const AllocaInst &AI) const {
    uint64_t ArraySize = 1;
    if (AI.isArrayAllocation()) {
        const ConstantInt *CI = dyn_cast<ConstantInt>(AI.getArraySize());
        assert(CI && "non-constant array size");
        ArraySize = CI->getZExtValue();
    }
    Type *Ty = AI.getAllocatedType();
    uint64_t SizeInBytes =
            AI.getModule()->getDataLayout().getTypeAllocSize(Ty);
    return SizeInBytes * ArraySize;
}

/// Check if we want (and can) handle this alloca.
bool DPPRule8L::isInterestingAlloca(const AllocaInst &AI) {
    auto PreviouslySeenAllocaInfo = ProcessedAllocas.find(&AI);

    if (PreviouslySeenAllocaInfo != ProcessedAllocas.end())
        return PreviouslySeenAllocaInfo->getSecond();

    bool IsInteresting =
            (AI.getAllocatedType()->isSized() &&
            // alloca() may be called with 0 size, ignore it.
            ((!AI.isStaticAlloca()) || getAllocaSizeInBytes(AI) > 0) &&
            // We are only interested in allocas not promotable to registers.
            // Promotable allocas are common under -O0.
            (!isAllocaPromotable(&AI)) &&
            // inalloca allocas are not treated as static, and we don't want
            // dynamic alloca instrumentation for them as well.
            !AI.isUsedWithInAlloca() &&
            // swifterror allocas are register promoted by ISel
            !AI.isSwiftError());

    ProcessedAllocas[&AI] = IsInteresting;
    return IsInteresting;
}


bool DPPRule8L::ignoreAccess(Value *Ptr) {
    // Do not instrument acesses from different address spaces; we cannot deal
    // with them.
    Type *PtrTy = cast<PointerType>(Ptr->getType()->getScalarType());
    if (PtrTy->getPointerAddressSpace() != 0)
        return true;

    // Ignore swifterror addresses.
    // swifterror memory addresses are mem2reg promoted by instruction
    // selection. As such they cannot have regular uses like an instrumentation
    // function and it makes no sense to track them as memory.
    if (Ptr->isSwiftError())
        return true;

    // Treat memory accesses to promotable allocas as non-interesting since they
    // will not cause memory violations. This greatly speeds up the instrumented
    // executable at -O0.
    if (auto AI = dyn_cast_or_null<AllocaInst>(Ptr))
        if (!isInterestingAlloca(*AI))
            return true;

        return false;
}


void DPPRule8L::getInterestingMemoryOperands(
        Instruction *I, SmallVectorImpl<InterestingMemoryOperand> &Interesting) {
    // Skip memory accesses inserted by another instrumentation.
    //if (I->hasMetadata("nosanitize"))
    //    return;

    // Do not instrument the load fetching the dynamic shadow address.
    //if (LocalDynamicShadow == I)
     //   return;

    if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
        if (ignoreAccess(LI->getPointerOperand()))
            return;
        Interesting.emplace_back(I, LI->getPointerOperandIndex(), false,
                                 LI->getType(), LI->getAlign());
    } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        if (ignoreAccess(SI->getPointerOperand()))
            return;
        Interesting.emplace_back(I, SI->getPointerOperandIndex(), true,
                                 SI->getValueOperand()->getType(), SI->getAlign());
    } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(I)) {
        if (ignoreAccess(RMW->getPointerOperand()))
            return;
        Interesting.emplace_back(I, RMW->getPointerOperandIndex(), true,
                                 RMW->getValOperand()->getType(), None);
    } else if (AtomicCmpXchgInst *XCHG = dyn_cast<AtomicCmpXchgInst>(I)) {
        if (ignoreAccess(XCHG->getPointerOperand()))
            return;
        Interesting.emplace_back(I, XCHG->getPointerOperandIndex(), true,
                                 XCHG->getCompareOperand()->getType(), None);
    } else if (auto CI = dyn_cast<CallInst>(I)) {
        auto *F = CI->getCalledFunction();
        if (F && (F->getName().startswith("llvm.masked.load.") || F->getName().startswith("llvm.masked.store."))) {
            bool IsWrite = F->getName().startswith("llvm.masked.store.");
            // Masked store has an initial operand for the value.
            unsigned OpOffset = IsWrite ? 1 : 0;
            //if (IsWrite ? !ClInstrumentWrites : !ClInstrumentReads)
              //  return;

            auto BasePtr = CI->getOperand(OpOffset);
            if (ignoreAccess(BasePtr))
                return;
            auto Ty = cast<PointerType>(BasePtr->getType())->getElementType();
            MaybeAlign Alignment = Align(1);
            // Otherwise no alignment guarantees. We probably got Undef.
            if (auto *Op = dyn_cast<ConstantInt>(CI->getOperand(1 + OpOffset)))
                Alignment = Op->getMaybeAlignValue();
            Value *Mask = CI->getOperand(2 + OpOffset);
            Interesting.emplace_back(I, OpOffset, IsWrite, Ty, Alignment, Mask);
        } else {
            for (unsigned ArgNo = 0; ArgNo < CI->getNumArgOperands(); ArgNo++) {
                if (!CI->isByValArgument(ArgNo) ||
                ignoreAccess(CI->getArgOperand(ArgNo)))
                    continue;
                Type *Ty = CI->getParamByValType(ArgNo);
                Interesting.emplace_back(I, ArgNo, false, Ty, Align(1));
            }
        }
    }
}

// isSafeAccess returns true if Addr is always inbounds with respect to its
// base object. For example, it is a field access or an array access with
// constant inbounds index.
bool DPPRule8L::isSafeAccess(ObjectSizeOffsetVisitor &ObjSizeVis,
                                    Value *Addr, uint64_t TypeSize) const {
    SizeOffsetType SizeOffset = ObjSizeVis.compute(Addr);
    if (!ObjSizeVis.bothKnown(SizeOffset)) return false;
    uint64_t Size = SizeOffset.first.getZExtValue();
    int64_t Offset = SizeOffset.second.getSExtValue();
    // Three checks are required to ensure safety:
    // . Offset >= 0  (since the offset is given from the base ptr)
    // . Size >= Offset  (unsigned)
    // . Size - Offset >= NeededSize  (unsigned)
    return Offset >= 0 && Size >= uint64_t(Offset) &&
    Size - uint64_t(Offset) >= TypeSize / 8;
}


DPPRule8L::Result DPPRule8L::run(Function &F, AnalysisManager<Function> &AM) {
    Result Result{};

    SmallPtrSet<Value *, 16> TempsToInstrument;
    SmallVector<InterestingMemoryOperand, 16> OperandsToInstrument;
    SmallVector<MemIntrinsic *, 16> IntrinToInstrument;
    SmallVector<Instruction *, 8> NoReturnCalls;
    SmallVector<BasicBlock *, 16> AllBlocks;
    SmallVector<Instruction *, 16> PointerComparisonsOrSubtracts;
    int NumAllocas = 0;

    // Fill the set of memory operations to instrument.
    for (auto &BB : F) {
        AllBlocks.push_back(&BB);
        TempsToInstrument.clear();
        int NumInsnsPerBB = 0;
        for (auto &Inst : BB) {
            //if (LooksLikeCodeInBug11395(&Inst)) return false;
            SmallVector<InterestingMemoryOperand, 1> InterestingOperands;
            getInterestingMemoryOperands(&Inst, InterestingOperands);

            if (!InterestingOperands.empty()) {
                for (auto &Operand : InterestingOperands) {
                    /*if (ClOpt && ClOptSameTemp)
                    {
                        Value *Ptr = Operand.getPtr();
                        // If we have a mask, skip instrumentation if we've already
                        // instrumented the full object. But don't add to TempsToInstrument
                        // because we might get another load/store with a different mask.
                        if (Operand.MaybeMask) {
                            if (TempsToInstrument.count(Ptr))
                                continue; // We've seen this (whole) temp in the current BB.
                        } else {
                            if (!TempsToInstrument.insert(Ptr).second)
                                continue; // We've seen this temp in the current BB.
                        }
                    }*/
                    OperandsToInstrument.push_back(Operand);
                    NumInsnsPerBB++;
                }
            } /*else if (isInterestingPointerComparison(&Inst) || isInterestingPointerSubtraction(&Inst)) {
                PointerComparisonsOrSubtracts.push_back(&Inst);
            } else if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(&Inst)) {
                // ok, take it.
                IntrinToInstrument.push_back(MI);
                NumInsnsPerBB++;
            } else {
                if (isa<AllocaInst>(Inst)) NumAllocas++;
                if (auto *CB = dyn_cast<CallBase>(&Inst)) {
                    // A call inside BB.
                    TempsToInstrument.clear();
                    if (CB->doesNotReturn() && !CB->hasMetadata("nosanitize"))
                        NoReturnCalls.push_back(CB);
                }
                //if (CallInst *CI = dyn_cast<CallInst>(&Inst))
                //    maybeMarkSanitizerLibraryCallNoBuiltin(CI, TLI);
            }*/
            /*if (NumInsnsPerBB >= ClMaxInsnsToInstrumentPerBB) break;*/
        }
    }

    const TargetLibraryInfo *TLI = &AM.getResult<TargetLibraryAnalysis>(F);
    const DataLayout &DL = F.getParent()->getDataLayout();
        ObjectSizeOpts ObjSizeOpts;
        ObjSizeOpts.RoundToAlign = true;
        ObjectSizeOffsetVisitor ObjSizeVis(DL, TLI, F.getContext(), ObjSizeOpts);

    int NumInstrumented = 0;
    for (auto &Operand : OperandsToInstrument) {
        Value *Addr = Operand.getPtr();
        if (!isSafeAccess(ObjSizeVis, Addr, Operand.TypeSize)) {
            Result.PrioritizedPtrMap.try_emplace(Addr, 1);
            //errs() << *Operand.getInsn() << " " << *Operand.getPtr() << "\n";
        }
    }

    return Result;
}

DenseSet<SVF::PAGNode *> DPPRule8G::getPointedObjectsByPtr(const Value *Ptr, SVFG *svfg) {
    DenseSet<SVF::PAGNode *> pointsToObjects;
    // get the points-to set
    NodeID pNodeId = svfg->getPAG()->getValueNode(Ptr);
    const NodeBS& pts = svfg->getPTA()->getPts(pNodeId);
    for (unsigned int pt : pts)
    {
        if (!svfg->getPAG()->hasGNode(pt))
            continue;

        PAGNode* targetObj = svfg->getPAG()->getPAGNode(pt);
        if(targetObj->hasValue())
        {
            //errs() << *targetObj << "\n";
            pointsToObjects.insert(targetObj);
        }
    }
    return pointsToObjects;
}

const VFGNode* DPPRule8G::getVFGNodeFromValue(PAG *pag, SVFG *svfg, const Value *val) {
    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(val));
    const VFGNode* vNode = svfg->getDefSVFGNode(pNode);
    return vNode;
}

DPPRule8G::Result DPPRule8G::run(Module &M, AnalysisManager<Module> &AM) {
    Result Result {};

    auto R = AM.getResult<SVFInitPass>(M);

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *Callgraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    LLVM_DEBUG(dbgs() << "Starting rule 8...\n");

    auto WhiteListResult = AM.getResult<DPPWhiteList>(M);
    auto FilteredObjs = AM.getResult<DPPRule1G>(M);

    // Collect the local results into our Result object
    auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

    LLVM_DEBUG(dbgs() << "Filtering out safely accessed buffers...\n");

    /// write some logs to file
    string dppLog = "#################### RULE 8 #########################\n";

    for (auto &F : M) {
        if (F.isDeclaration()) {
            continue; // Skip declarations (i.e., functions without a definition)
        }

        auto &R = FAM.getResult<DPPRule8L>(F);

        for (auto Item: R.PrioritizedPtrMap) {
            auto *const Val = Item.getFirst();

            if (WhiteListResult.isSafe(Val)) {
                //errs() << "    " << *Val << "\n";
                continue;
            }

            /// get objects pointed by the operand and store the objects in the pointer map
            auto objPointsToSet = getPointedObjectsByPtr(Val, svfg);
            for (auto Item: objPointsToSet) {
                // to filter some objects that may be safe
                if (WhiteListResult.isSafe(Item->getValue())) {
                    //errs() << "    " << *Val << "\n";
                    continue;
                }
                // if the item object is in filtered list, then add it to result
                if (FilteredObjs.PrioritizedPtrMap.find(Item->getValue()) != FilteredObjs.PrioritizedPtrMap.end()) {
                    Result.PrioritizedPtrMap.try_emplace(Item->getValue(), 1);

                    auto SVFNode = getVFGNodeFromValue(pag, svfg, Item->getValue());
                    dppLog += SVFNode->toString() + "\n";
                    dppLog += "--------------------------------------------------------------\n";
                }
            }
        }
    }

    dppLog += "##################################################\n\n\n";
    if (DPP::isLogIndividualRule())
        DPP::writeDPPLogsToFile(dppLog);

    //errs() << "Rule 8 = " << Result.PrioritizedPtrMap.size() << ", Filtered = "
    //<< FilteredObjs.PrioritizedPtrMap.size() << "\n";

    return Result;
}

raw_ostream &DPPRule8GResult::print(raw_ostream &OS) const {

    return OS;
}