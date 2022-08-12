//
// Created by salman on 6/29/21.
//

#ifndef DPP_LLVM_DPPINSTR_H
#define DPP_LLVM_DPPINSTR_H

#include "llvm/DPP/DPPUtils.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"

#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"

namespace SVF {
    class SVFG;
} // namespace SVF

namespace llvm {
namespace DPP {

    class DPPInstr : public PassInfoMixin<DPPInstr> {
    public:
        PreservedAnalyses run(Module &M, AnalysisManager<Module> &AM);

    private:
        ValSet GetCompleteUsers(const Value *Val, SVF::SVFG *svfg);
        ValSet getPointersToObject(const Value *Val, SVF::SVFG *svfg);
        inline bool handleInstruction(Function &F, Instruction &I);
        bool handleStoreInstruction(Function &F, StoreInst *pSI);
        bool handleLoadInstruction(Function &F, LoadInst *pLI);
        CallInst *createPartsIntrinsic(Function &F, Instruction &I, Value *calledValue, Intrinsic::ID intrinsicID);
        CallInst *createPartsIntrinsicNoModifier(Function &F, Instruction &I, Value *calledValue,
                                                 Intrinsic::ID intrinsicID);
        bool isUnionMemberLoad(LoadInst *load);
        Constant *getZeroConstant(LLVMContext &C);
    };

} // namespace DPP
} // namespace llvm


#endif //DPP_LLVM_DPPINSTR_H