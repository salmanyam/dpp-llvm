//
// Created by salman on 7/5/21.
//

#ifndef DPP_LLVM_DPPRULE9_H
#define DPP_LLVM_DPPRULE9_H

#include <vector>
#include <set>
#include <list>
#include "llvm/DPP/DPPRule.h"
#include "llvm/IR/PassManager.h"


namespace SVF {
    class PAG;
    class PTACallGraph;
    class SVFG;
    class VFGNode;
    class ICFGNode;
} // namespace SVF

namespace llvm {
    namespace DPP {

        class DPPRule9GResult;

        class DPPRule9G : public AnalysisInfoMixin<DPPRule9G> {
            friend AnalysisInfoMixin<DPPRule9G>;
        public:
            using Result = DPPRule9GResult;

            static const char RuleName[];
            static AnalysisKey Key;

            Result run(Module &M, AnalysisManager<Module> &AM);
            auto getReachableNodes(const SVF::VFGNode* vNode, SVF::PAG *pag, SVF::SVFG *svfg);
            auto assignDepth(const SVF::ICFGNode *firstNode, unsigned long long depth);
            auto getCmpPaths(const SVF::ICFGNode *icfgNode);
            int getNumCmpsInPath(std::vector<const SVF::ICFGNode *> );
            auto getVFGNodeFromValue(SVF::PAG *pag, SVF::SVFG *svfg, const Value *val);
            bool checkCommonAncestor(std::list<const SVF::VFGNode *> vNodes, std::set<uint32_t> toCompare);
            auto getCmpInstructions(const SVF::ICFGNode *icfgNode);
        };

        class DPPRule9GResult : public DPPResult<DPPRule9G> {
            friend DPPRule9G;
        public:
            llvm::DPP::DPPMap PrioritizedPtrMap;
        public:
            DPPRule9GResult() {}
            raw_ostream &print(raw_ostream &OS) const;
        };

        class [[maybe_unused]] DPPRule9GPrinterPass
                : public DPPGlobalPrinterPass<DPPRule9G> {
        public:
            [[maybe_unused]] DPPRule9GPrinterPass(raw_ostream &OS)
                    : DPPGlobalPrinterPass(OS) {}
        };

    } // namespace DPP
} // namespace llvm

#endif //DPP_LLVM_DPPRULE9_H
