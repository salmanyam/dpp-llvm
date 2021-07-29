//
// Created by salman on 7/13/21.
//

#include "llvm/DPP/SVFInitPass.h"
#include "llvm/DPP/DPPRule8.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <vector>
#include <utility>
#include "llvm/ADT/SmallVector.h"

#include "llvm/Transforms/InstCombine/InstCombiner.h"

#define DEBUG_TYPE "DPPRule8"

using namespace llvm;
using namespace llvm::DPP;
using namespace SVF;


[[maybe_unused]] const char DPPRule8G::RuleName[] = "DPPRule8G";
AnalysisKey DPPRule8G::Key;


ValSet DPPRule8G::getPointersToObject(const Value *Val, SVFG *svfg) {
    /// get all the pointers pointing to an object, i.e., the object pointed by Val
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

ValSet DPPRule8G::GetCompleteUsers(const Value *Val, SVFG *svfg) {
    FIFOWorkList<const Value *> worklist;
    ValSet visited;

    /// insert the Val in the worklist as we need to check its existence as well
    worklist.push(Val);

    /// get all the pointers pointing to an object, i.e., the object pointed by Val
    auto Pointers = getPointersToObject(Val, svfg);
    for (auto Ptr: Pointers) {
        if (visited.find(Ptr) == visited.end())
            worklist.push(Ptr);
    }

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


// isSafeAccess returns true if Addr is always inbounds with respect to its
// base object. For example, it is a field access or an array access with
// constant inbounds index.
static bool isSafeAccess(ObjectSizeOffsetVisitor &ObjSizeVis,
                         Value *Addr, uint64_t TypeSize) {
    SizeOffsetType SizeOffset = ObjSizeVis.compute(Addr);
    if (!ObjSizeVis.bothKnown(SizeOffset)) {
        errs() << "Inside unknown" << "\n";
        return false;
    }
    errs() << "Outside unknown\n";
    uint64_t Size = SizeOffset.first.getZExtValue();
    int64_t Offset = SizeOffset.second.getSExtValue();
    // Three checks are required to ensure safety:
    // . Offset >= 0  (since the offset is given from the base ptr)
    // . Size >= Offset  (unsigned)
    // . Size - Offset >= NeededSize  (unsigned)
    return Offset >= 0 && Size >= uint64_t(Offset) &&
           Size - uint64_t(Offset) >= TypeSize / 8;
}


/// Gets the conditions under which memory accessing instructions will overflow.
///
/// \p Ptr is the pointer that will be read/written, and \p InstVal is either
/// the result from the load or the value being stored. It is used to determine
/// the size of memory block that is touched.
///
/// Returns the condition under which the access will overflow.
static Value *getBoundsCheckCond(Value *Ptr, Value *InstVal, Type *OpType,
                                 const DataLayout &DL, TargetLibraryInfo &TLI,
                                 ObjectSizeOffsetEvaluator &ObjSizeEval, ObjectSizeOffsetVisitor &ObjSizeVisitor,
                                 /*IRBuilder<TargetFolder> &IRB,*/ ScalarEvolution &SE) {

    uint64_t TypeSize = DL.getTypeStoreSizeInBits(OpType);

    uint64_t NeededSize = DL.getTypeStoreSize(InstVal->getType());
    LLVM_DEBUG(dbgs() << "Instrument " << *Ptr << " for " << Twine(NeededSize) << " bytes\n");

    //errs() << "Instrument " << *Ptr << " for " << Twine(NeededSize) << " bytes\n";


    if(isSafeAccess(ObjSizeVisitor, Ptr, TypeSize)) {
        errs() << "Safe use: " << *Ptr << ": " << *InstVal << "\n";
    }
    else {
        errs() << "Unsafe use: " << *Ptr << ": " << *InstVal << "\n";
    }


    /*
    SizeOffsetEvalType SizeOffset = ObjSizeEval.compute(Ptr);

    if (!ObjSizeEval.bothKnown(SizeOffset)) {
        ++ChecksUnable;
        errs() << "Check unable\n";
        return nullptr;
    }

    Value *Size   = SizeOffset.first;
    Value *Offset = SizeOffset.second;
    ConstantInt *SizeCI = dyn_cast<ConstantInt>(Size);

    Type *IntTy = DL.getIntPtrType(Ptr->getType());
    Value *NeededSizeVal = ConstantInt::get(IntTy, NeededSize);

    auto SizeRange = SE.getUnsignedRange(SE.getSCEV(Size));
    auto OffsetRange = SE.getUnsignedRange(SE.getSCEV(Offset));
    auto NeededSizeRange = SE.getUnsignedRange(SE.getSCEV(NeededSizeVal));

    errs() << "Printing sizes...\n";
    errs() << *Size << " " << *Offset << " " << *SizeCI << " " << *NeededSizeVal << " " << SizeRange << " " << OffsetRange << " " << NeededSizeRange << "\n";
*/
    // three checks are required to ensure safety:
    // . Offset >= 0  (since the offset is given from the base ptr)
    // . Size >= Offset  (unsigned)
    // . Size - Offset >= NeededSize  (unsigned)
    //
    // optimization: if Size >= 0 (signed), skip 1st check
    // FIXME: add NSW/NUW here?  -- we dont care if the subtraction overflows
    /*Value *ObjSize = IRB.CreateSub(Size, Offset);
    Value *Cmp2 = SizeRange.getUnsignedMin().uge(OffsetRange.getUnsignedMax())
                  ? ConstantInt::getFalse(Ptr->getContext())
                  : IRB.CreateICmpULT(Size, Offset);
    Value *Cmp3 = SizeRange.sub(OffsetRange)
                          .getUnsignedMin()
                          .uge(NeededSizeRange.getUnsignedMax())
                  ? ConstantInt::getFalse(Ptr->getContext())
                  : IRB.CreateICmpULT(ObjSize, NeededSizeVal);
    Value *Or = IRB.CreateOr(Cmp2, Cmp3);
    if ((!SizeCI || SizeCI->getValue().slt(0)) &&
        !SizeRange.getSignedMin().isNonNegative()) {
        Value *Cmp1 = IRB.CreateICmpSLT(Offset, ConstantInt::get(IntTy, 0));
        Or = IRB.CreateOr(Cmp1, Or);
    }

    return Or;*/
    return nullptr;
}


static bool addBoundsChecking(/*const Instruction *TargetInst,*/ Function &F, TargetLibraryInfo &TLI,
                              ScalarEvolution &SE) {
    const DataLayout &DL = F.getParent()->getDataLayout();
    ObjectSizeOpts EvalOpts;
    EvalOpts.RoundToAlign = true;
    ObjectSizeOffsetEvaluator ObjSizeEval(DL, &TLI, F.getContext(), EvalOpts);
    ObjectSizeOffsetVisitor ObjSizeVis(DL, &TLI, F.getContext(), EvalOpts);

    // check HANDLE_MEMORY_INST in include/llvm/Instruction.def for memory
    // touching instructions
    //SmallVector<std::pair<Instruction *, Value *>, 4> TrapInfo;
    //SmallVector<Instruction *, 4> TrapInfo;
    //SmallVector<Instruction *,16> InstToDelete;
    for (Instruction &I : instructions(F)) {
        //if (&I != TargetInst)
            //continue;

        //errs() << I << "\n";
        Value *Or = nullptr;
        //BuilderTy IRB(I.getParent(), BasicBlock::iterator(&I), TargetFolder(DL));
        //IRBuilder<> Builder(&I);
        if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
            if (!LI->isVolatile())
                Or = getBoundsCheckCond(LI->getPointerOperand(), LI, LI->getType(), DL, TLI,
                                        ObjSizeEval, ObjSizeVis, /*IRB,*/ SE);
        } else if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
            if (!SI->isVolatile())
                Or = getBoundsCheckCond(SI->getPointerOperand(), SI->getValueOperand(), SI->getType(),
                                        DL, TLI, ObjSizeEval, ObjSizeVis, /*IRB,*/ SE);
        } else if (AtomicCmpXchgInst *AI = dyn_cast<AtomicCmpXchgInst>(&I)) {
            if (!AI->isVolatile())
                Or =
                        getBoundsCheckCond(AI->getPointerOperand(), AI->getCompareOperand(), AI->getType(),
                                           DL, TLI, ObjSizeEval, ObjSizeVis, /*IRB,*/ SE);
        } else if (AtomicRMWInst *AI = dyn_cast<AtomicRMWInst>(&I)) {
            if (!AI->isVolatile())
                Or = getBoundsCheckCond(AI->getPointerOperand(), AI->getValOperand(), AI->getType(),
                                        DL, TLI, ObjSizeEval, ObjSizeVis, /*IRB,*/ SE);
        }
        //if (Or)
          //  TrapInfo.push_back(std::make_pair(&I, Or));
   }
/*
    // Create a trapping basic block on demand using a callback. Depending on
    // flags, this will either create a single block for the entire function or
    // will create a fresh block every time it is called.
    BasicBlock *TrapBB = nullptr;
    auto GetTrapBB = [&TrapBB](BuilderTy &IRB) {
        if (TrapBB && SingleTrapBB)
            return TrapBB;

        Function *Fn = IRB.GetInsertBlock()->getParent();
        // FIXME: This debug location doesn't make a lot of sense in the
        // `SingleTrapBB` case.
        auto DebugLoc = IRB.getCurrentDebugLocation();
        IRBuilder<>::InsertPointGuard Guard(IRB);
        TrapBB = BasicBlock::Create(Fn->getContext(), "trap", Fn);
        IRB.SetInsertPoint(TrapBB);

        auto *F = Intrinsic::getDeclaration(Fn->getParent(), Intrinsic::trap);
        CallInst *TrapCall = IRB.CreateCall(F, {});
        TrapCall->setDoesNotReturn();
        TrapCall->setDoesNotThrow();
        TrapCall->setDebugLoc(DebugLoc);
        IRB.CreateUnreachable();

        return TrapBB;
    };

    // Add the checks.
    for (const auto &Entry : TrapInfo) {
        Instruction *Inst = Entry.first;
        BuilderTy IRB(Inst->getParent(), BasicBlock::iterator(Inst), TargetFolder(DL));
        insertBoundsCheck(Entry.second, IRB, GetTrapBB);
    }

    return !TrapInfo.empty();
    */

   return false;
}


DPPRule8G::Result DPPRule8G::run(Module &M, AnalysisManager<Module> &AM) {
    Result Result {};

    auto R = AM.getResult<SVFInitPass>(M);

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *Callgraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    auto DPValues = GetDataPointerInstructions(svfg, false);

    /// store the users of a value to a map
    ValUserMap VUMap;
    for (auto DPVal: DPValues) {
        /// DPusers list also include DPInst as a user
        //auto DPUsers = GetCompleteUsers(DPVal, svfg);
        //VUMap.try_emplace(DPVal, DPUsers);
        errs() << *DPVal << "\n";
    }
/*
    errs() << "Printing value and its users\n";
    for (auto Item: VUMap) {
        auto Users = Item.getSecond();
        errs() << "\nValue: " << *Item.getFirst() << "\n";
        errs() << "---------------------------\n";
        for (auto User: Users) {
            errs() << "User: " << *User << "\n";
        }
    }
    errs() << "Printing end\n";*/

    ValSet AlreadyCovered;

    auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
    for (auto &F: M) {
        // skip the intrinsic or declaration functions
        if (F.isIntrinsic() || F.isDeclaration())
            continue;

        errs() << F.getName() << "\n";
        errs() << "------------------------------\n";

        auto &TLI = FAM.getResult<TargetLibraryAnalysis>(F);
        auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);

        addBoundsChecking(F, TLI, SE);

        /*
        /// For each data pointer instruction, check its users existence in loops
        for (auto Item: VUMap) {
            auto Users = Item.getSecond();
            errs() << "value = " << *Item.getFirst() << "\n";
            /// skip the items that have been already tested and they have some values which have been used in loops
            if (AlreadyCovered.find(Item.getFirst()) != AlreadyCovered.end())
                continue;

            for (auto User: Users) {
                //errs() << "User: " << *User << "\n";
                if (auto *I = dyn_cast<Instruction>(User)) {
                    if (I->getFunction() == &F) {
                        //errs() << I->getFunction()->getName() << " " << F.getName() << "\n";

                        addBoundsChecking(I, F, TLI, SE);
                    }
                }
            }
        }*/


    }



    errs() << "Final results \n";
    for (auto Item: AlreadyCovered) {
        errs() << *Item << "\n";
    }

    return Result;
}

raw_ostream &DPPRule8GResult::print(raw_ostream &OS) const {

    return OS;
}