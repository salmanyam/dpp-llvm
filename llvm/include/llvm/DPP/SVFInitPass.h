//
// Created by salman on 7/26/21.
//

#ifndef DPP_LLVM_SVFINITPASS_H
#define DPP_LLVM_SVFINITPASS_H

#include "llvm/DPP/DPPRule.h"

namespace SVF {
    class PAG;
    class PTACallGraph;
    class SVFG;
} // namespace SVF

namespace llvm {
    namespace DPP {

        struct SVFParams_t {
            SVF::PAG *pag = nullptr;
            SVF::PTACallGraph *CallGraph = nullptr;
            SVF::SVFG *svfg = nullptr;
        };

        class SVFInitPassResult;

        class SVFInitPass : public AnalysisInfoMixin<SVFInitPass> {
            friend AnalysisInfoMixin<SVFInitPass>;
        public:
            using Result = SVFInitPassResult;

            static const char RuleName[];
            static AnalysisKey Key;

            Result run(Module &M, AnalysisManager<Module> &AM);
        };

        class SVFInitPassResult : public DPPResult<SVFInitPass> {
            friend SVFInitPass;
        public:
            struct SVFParams_t SVFParams;
        public:
            SVFInitPassResult() {}
            raw_ostream &print(raw_ostream &OS) const;
        };
    } // namespace DPP
} // namespace llvm


#endif //DPP_LLVM_SVFINITPASS_H
