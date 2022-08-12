//
// Created by salman on 8/10/22.
//

#ifndef DPP_LLVM_ASAN_FUNC_ATTR_H
#define DPP_LLVM_ASAN_FUNC_ATTR_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"

#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"


namespace llvm {
namespace DPP {

    class AsanFuncAttr : public PassInfoMixin<AsanFuncAttr> {
    public:
        PreservedAnalyses run(Module &M, AnalysisManager<Module> &AM);
    };

} // namespace DPP
} // namespace llvm


#endif //DPP_LLVM_ASAN_FUNC_ATTR_H
