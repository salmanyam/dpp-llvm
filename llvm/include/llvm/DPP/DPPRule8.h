//
// Created by salman on 7/13/21.
//

#ifndef DPP_LLVM_DPPRULE8_H
#define DPP_LLVM_DPPRULE8_H


#include "llvm/DPP/DPPUtils.h"
#include "llvm/DPP/DPPRule.h"

#include "llvm/IR/PassManager.h"

namespace SVF {
    class PAG;
    class PTACallGraph;
    class SVFG;
} // namespace SVF

namespace llvm {
    namespace DPP {

        class DPPRule8GResult;

        class DPPRule8G : public AnalysisInfoMixin<DPPRule8G> {
            friend AnalysisInfoMixin<DPPRule8G>;
        public:
            using Result = DPPRule8GResult;

            static const char RuleName[];
            static AnalysisKey Key;

            Result run(Module &M, AnalysisManager<Module> &AM);
            llvm::DPP::ValSet getPointersToObject(const Value *Val, SVF::SVFG *svfg);
            llvm::DPP::ValSet GetCompleteUsers(const Value *Val, SVF::SVFG *svfg);
        };

        class DPPRule8GResult : public DPPResult<DPPRule8G> {
            friend DPPRule8G;
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
