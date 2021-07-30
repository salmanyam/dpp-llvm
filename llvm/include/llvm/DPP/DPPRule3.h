//
// Created by salman on 7/4/21.
//

#ifndef DPP_LLVM_DPPRULE3_H
#define DPP_LLVM_DPPRULE3_H

#include "llvm/DPP/DPPUtils.h"
#include "llvm/DPP/DPPRule.h"

#include "llvm/IR/PassManager.h"
#include <llvm/Analysis/LoopInfo.h>
#include "set"

namespace SVF {
    class PAG;
    class VFGNode;
    class SVFG;
} // namespace SVF

namespace llvm {
    namespace DPP {

        class DPPRule3GResult;

        class DPPRule3G : public AnalysisInfoMixin<DPPRule3G> {
            friend AnalysisInfoMixin<DPPRule3G>;
        public:
            using Result = DPPRule3GResult;
            using LoopSet = DenseSet<Loop *>;

            static const char RuleName[];
            static AnalysisKey Key;

            Result run(Module &M, AnalysisManager<Module> &AM);
            const SVF::VFGNode* getVFGNodeFromValue(SVF::PAG *pag, SVF::SVFG *svfg, const Value *val);
            llvm::DPP::ValSet getPointersToObject(const Value *Val, SVF::SVFG *svfg);
            llvm::DPP::ValSet GetCompleteUsers(const Value *Val, SVF::SVFG *svfg);
            bool IsUsedInLoops(const Instruction *Val);
            bool CheckInstInLoopPredecessor(Loop *, const Instruction *);
            bool CheckInstInLoopHeader(Loop *, const Instruction *);
            bool CheckInstInLoopLatch(Loop *, const Instruction *);
            bool CheckUsageOfDPInstInLoop(const Instruction *, LoopSet LS);
            LoopSet TraverseLoops(LoopInfo &Loops);
        };

        class DPPRule3GResult : public DPPResult<DPPRule3G> {
            friend DPPRule3G;
        public:
            llvm::DPP::DPPMap PrioritizedPtrMap;
        public:
            DPPRule3GResult() {}
            raw_ostream &print(raw_ostream &OS) const;
        };

        class [[maybe_unused]] DPPRule3GPrinterPass
                : public DPPGlobalPrinterPass<DPPRule3G> {
        public:
            [[maybe_unused]] DPPRule3GPrinterPass(raw_ostream &OS)
                    : DPPGlobalPrinterPass(OS) {}
        };

    } // namespace DPP
} // namespace llvm


#endif //DPP_LLVM_DPPRULE3_H
