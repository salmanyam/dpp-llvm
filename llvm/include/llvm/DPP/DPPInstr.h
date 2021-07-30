//
// Created by salman on 6/29/21.
//

#ifndef DPP_LLVM_DPPINSTR_H
#define DPP_LLVM_DPPINSTR_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"

#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"


namespace llvm {
namespace DPP {

    class DPPInstr : public PassInfoMixin<DPPInstr> {
    public:
        PreservedAnalyses run(Function &F, AnalysisManager<Function> &AM);

    private:
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