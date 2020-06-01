//
// Created by ishkamiel on 09/06/2020.
//

#ifndef LLVM_DPPRULE_H
#define LLVM_DPPRULE_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace DPP {

/// Interface for interacting with Rule results
template <typename AnalysisT,
          typename ResultT = typename AnalysisT::Result>
class DPPResult {
public:
  inline raw_ostream &print(raw_ostream &OS) const {
    return static_cast<ResultT*>(this)->print(OS);
  }
  constexpr const char *getName() const { return AnalysisT::RuleName; };
};

/// Base template implementation of printer pass
template <typename AnalysisT,
    typename = std::enable_if<std::is_base_of<DPPResult<AnalysisT>,
        typename AnalysisT::Result>::value, AnalysisT>>
class DPPLocalPrinterPass
    : public PassInfoMixin<DPPLocalPrinterPass<AnalysisT>> {
private:
  raw_ostream &OS;

public:
  explicit DPPLocalPrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Function &F, AnalysisManager<Function> &AM) {
    auto Result = AM.getResult<AnalysisT>(F);
    OS << Result.getName() << "\n";
    OS << F.getName() << ":\n";
    Result.print(OS);
    return PreservedAnalyses::all();
  }
};

} // namespace DPP
} // namespace llvm

#endif // LLVM_DPPRULE_H
