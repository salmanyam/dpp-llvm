//
// Created by salman on 8/10/22.
//

#include "llvm/DPP/AsanFuncAttr.h"


using namespace llvm;
using namespace llvm::DPP;

#define DEBUG_TYPE "AsanFuncAttr"

PreservedAnalyses AsanFuncAttr::run(Module &M, AnalysisManager<Module> &AM) {
    bool function_modified = false;

    for (auto &F : M) {
	if (F.isDeclaration() || F.isIntrinsic())
	    continue;

        if (!F.hasFnAttribute("sanitize_address")) {
	    F.addFnAttr(llvm::Attribute::SanitizeAddress);
            function_modified = true;
            continue;
        }
    }

    return function_modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

