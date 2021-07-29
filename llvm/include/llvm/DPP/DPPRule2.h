//
// Created by salman on 7/1/21.
//

#ifndef DPP_LLVM_DPPRULE2_H
#define DPP_LLVM_DPPRULE2_H

#include "llvm/DPP/DPPUtils.h"
#include "llvm/DPP/DPPRule.h"

#include "llvm/IR/PassManager.h"

namespace SVF {
    class PAG;
    class VFGNode;
    class SVFG;
} // namespace SVF

namespace llvm {
    namespace DPP {

        class DPPRule2GResult;

        class DPPRule2G : public AnalysisInfoMixin<DPPRule2G> {
            friend AnalysisInfoMixin<DPPRule2G>;
        public:
            using Result = DPPRule2GResult;

            static const char RuleName[];
            static AnalysisKey Key;

            Result run(Module &M, AnalysisManager<Module> &AM);
            const SVF::VFGNode* getVFGNodeFromValue(SVF::PAG *pag, SVF::SVFG *svfg, const Value *val);
            llvm::DPP::ValSet getPointersToObject(const Value *Val, SVF::SVFG *svfg);
            llvm::DPP::ValSet GetCompleteUsers(const Value *Val, SVF::SVFG *svfg);
        };

        class DPPRule2GResult : public DPPResult<DPPRule2G> {
            friend DPPRule2G;
        public:
            llvm::DPP::DPPMap PrioritizedPtrMap;
        public:
            DPPRule2GResult() {}
            raw_ostream &print(raw_ostream &OS) const;
        };

        class [[maybe_unused]] DPPRule2GPrinterPass
                : public DPPGlobalPrinterPass<DPPRule2G> {
        public:
            [[maybe_unused]] DPPRule2GPrinterPass(raw_ostream &OS)
                    : DPPGlobalPrinterPass(OS) {}
        };

    } // namespace DPP
} // namespace llvm

#endif //DPP_LLVM_DPPRULE2_H
