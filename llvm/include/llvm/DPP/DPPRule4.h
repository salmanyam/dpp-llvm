//
// Created by salman on 7/1/21.
//

#ifndef DPP_LLVM_DPPRULE4_H
#define DPP_LLVM_DPPRULE4_H

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

class DPPRule4GResult;

class DPPRule4G : public AnalysisInfoMixin<DPPRule4G> {
    friend AnalysisInfoMixin<DPPRule4G>;
public:
    using Result = DPPRule4GResult;

    static const char RuleName[];
    static AnalysisKey Key;

    Result run(Module &M, AnalysisManager<Module> &AM);
};

class DPPRule4GResult : public DPPResult<DPPRule4G> {
    friend DPPRule4G;
public:
    llvm::DPP::DPPMap PrioritizedPtrMap;
public:
    DPPRule4GResult() {}
    raw_ostream &print(raw_ostream &OS) const;
};

class [[maybe_unused]] DPPRule4GPrinterPass
        : public DPPGlobalPrinterPass<DPPRule4G> {
public:
    [[maybe_unused]] DPPRule4GPrinterPass(raw_ostream &OS)
            : DPPGlobalPrinterPass(OS) {}
};

} // namespace DPP
} // namespace llvm

#endif //DPP_LLVM_DPPRULE4_H
