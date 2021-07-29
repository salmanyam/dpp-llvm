//
// Created by salman on 7/5/21.
//

#ifndef DPP_LLVM_DPPRULE6_H
#define DPP_LLVM_DPPRULE6_H


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

        class DPPRule6GResult;

        class DPPRule6G : public AnalysisInfoMixin<DPPRule6G> {
            friend AnalysisInfoMixin<DPPRule6G>;
        public:
            using Result = DPPRule6GResult;

            static const char RuleName[];
            static AnalysisKey Key;

            Result run(Module &M, AnalysisManager<Module> &AM);
            const SVF::VFGNode* getVFGNodeFromValue(SVF::PAG *pag, SVF::SVFG *svfg, const Value *val);
            llvm::DPP::ValSet getPointersToObject(const Value *Val, SVF::SVFG *svfg);
            llvm::DPP::ValSet GetCompleteUsers(const Value *Val, SVF::SVFG *svfg);
            DenseSet<StringRef> GetBlackListFunctions();
        };

        class DPPRule6GResult : public DPPResult<DPPRule6G> {
            friend DPPRule6G;
        public:
            llvm::DPP::DPPMap PrioritizedPtrMap;
        public:
            DPPRule6GResult() {}
            raw_ostream &print(raw_ostream &OS) const;
        };

        class [[maybe_unused]] DPPRule6GPrinterPass
                : public DPPGlobalPrinterPass<DPPRule6G> {
        public:
            [[maybe_unused]] DPPRule6GPrinterPass(raw_ostream &OS)
                    : DPPGlobalPrinterPass(OS) {}
        };

    } // namespace DPP
} // namespace llvm


#endif //DPP_LLVM_DPPRULE6_H
