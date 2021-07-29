//
// Created by salman on 7/12/21.
//

#ifndef DPP_LLVM_DPPRULE7_H
#define DPP_LLVM_DPPRULE7_H

#include "llvm/DPP/DPPUtils.h"
#include "llvm/DPP/DPPRule.h"

#include "llvm/ADT/SmallSet.h"
#include "llvm/IR/PassManager.h"

namespace SVF {
    class PAG;
    class VFGNode;
    class SVFG;
} // namespace SVF

namespace llvm {
    namespace DPP {

        class DPPRule7GResult;

        class DPPRule7G : public AnalysisInfoMixin<DPPRule7G> {
            friend AnalysisInfoMixin<DPPRule7G>;
        public:
            using Result = DPPRule7GResult;

            static const char RuleName[];
            static AnalysisKey Key;

            Result run(Module &M, AnalysisManager<Module> &AM);
            const SVF::VFGNode* getVFGNodeFromValue(SVF::PAG *pag, SVF::SVFG *svfg, const Value *val);
            llvm::DPP::ValSet getPointersToObject(const Value *Val, SVF::SVFG *svfg);
            llvm::DPP::ValSet GetCompleteUsers(const Value *Val, SVF::SVFG *svfg);
            bool HasUnsafeCasting(const Instruction *I, Module &M);
        };

        class DPPRule7GResult : public DPPResult<DPPRule7G> {
            friend DPPRule7G;
        public:
            llvm::DPP::DPPMap PrioritizedPtrMap;
        public:
            DPPRule7GResult() {}
            raw_ostream &print(raw_ostream &OS) const;
        };

        class [[maybe_unused]] DPPRule7GPrinterPass
                : public DPPGlobalPrinterPass<DPPRule7G> {
        public:
            [[maybe_unused]] DPPRule7GPrinterPass(raw_ostream &OS)
                    : DPPGlobalPrinterPass(OS) {}
        };

    } // namespace DPP
} // namespace llvm


#endif //DPP_LLVM_DPPRULE7_H
