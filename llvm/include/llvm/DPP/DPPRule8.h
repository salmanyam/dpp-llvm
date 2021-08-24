//
// Created by salman on 7/13/21.
//

#ifndef DPP_LLVM_DPPRULE8_H
#define DPP_LLVM_DPPRULE8_H


#include "llvm/DPP/DPPUtils.h"
#include "llvm/DPP/DPPRule.h"

#include "llvm/IR/PassManager.h"

#include "llvm/Analysis/MemoryBuiltins.h"

namespace SVF {
    class PAG;
    class PTACallGraph;
    class SVFG;
} // namespace SVF

namespace llvm {
namespace DPP {


class InterestingMemoryOperand {
public:
    Use *PtrUse;
    bool IsWrite;
    Type *OpType;
    uint64_t TypeSize;
    MaybeAlign Alignment;
    // The mask Value, if we're looking at a masked load/store.
    Value *MaybeMask;

    InterestingMemoryOperand(Instruction *I, unsigned OperandNo, bool IsWrite,
                             class Type *OpType, MaybeAlign Alignment,
                                     Value *MaybeMask = nullptr)
                                     : IsWrite(IsWrite), OpType(OpType), Alignment(Alignment),
                                     MaybeMask(MaybeMask) {
        const DataLayout &DL = I->getModule()->getDataLayout();
        TypeSize = DL.getTypeStoreSizeInBits(OpType);
        PtrUse = &I->getOperandUse(OperandNo);
    }

    Instruction *getInsn() { return cast<Instruction>(PtrUse->getUser()); }

    Value *getPtr() { return PtrUse->get(); }
};

class DPPRule8LResult;
class DPPRule8GResult;

class DPPRule8L : public AnalysisInfoMixin<DPPRule8L> {
    friend AnalysisInfoMixin<DPPRule8L>;
private:
    DenseMap<const AllocaInst *, bool> ProcessedAllocas;
public:
    using Result = DPPRule8LResult;

    static const char RuleName[];
    static AnalysisKey Key;

    Result run(Function &F, AnalysisManager<Function> &AM);
    bool isPointerOperand(Value *V) {
        return V->getType()->isPointerTy() || isa<PtrToIntInst>(V);
    }
    bool isInterestingPointerComparison(Instruction *I);
    bool isInterestingPointerSubtraction(Instruction *I);
    uint64_t getAllocaSizeInBytes(const AllocaInst &AI) const;
    bool isInterestingAlloca(const AllocaInst &AI);
    bool ignoreAccess(Value *Ptr);
    bool isSafeAccess(ObjectSizeOffsetVisitor &ObjSizeVis, Value *Addr, uint64_t TypeSize) const;
    void getInterestingMemoryOperands(Instruction *I, SmallVectorImpl<InterestingMemoryOperand> &Interesting);
};

class DPPRule8LResult : public DPPResult<DPPRule8L> {
    friend DPPRule8L;
public:
    llvm::DPP::DPPMap PrioritizedPtrMap;
public:
    DPPRule8LResult() {}
    raw_ostream &print(raw_ostream &OS) const;
};


class DPPRule8G : public AnalysisInfoMixin<DPPRule8G> {
    friend AnalysisInfoMixin<DPPRule8G>;

public:
    using Result = DPPRule8GResult;

    static const char RuleName[];
    static AnalysisKey Key;

    Result run(Module &M, AnalysisManager<Module> &AM);
    DenseSet<SVF::PAGNode *> getPointedObjectsByPtr(const Value *Ptr, SVF::SVFG *svfg);
    const SVF::VFGNode* getVFGNodeFromValue(SVF::PAG *pag, SVF::SVFG *svfg, const Value *val);
};

class DPPRule8GResult : public DPPResult<DPPRule8G> {
    friend DPPRule8G;
public:
    llvm::DPP::DPPMap PrioritizedPtrMap;
public:
    DPPRule8GResult() {}
    raw_ostream &print(raw_ostream &OS) const;
};

class [[maybe_unused]] DPPRule8GPrinterPass
        : public DPPGlobalPrinterPass<DPPRule8G> {
public:
    [[maybe_unused]] DPPRule8GPrinterPass(raw_ostream &OS)
            : DPPGlobalPrinterPass(OS) {}
};


} // namespace DPP
} // namespace llvm



#endif //DPP_LLVM_DPPRULE8_H
