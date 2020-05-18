//
// Created by ishkamiel on 18/05/2020.
//

#include "llvm/Analysis/DPP/DPP.h"

using namespace llvm;
using namespace DPP;

namespace {

class Rule6Result : public DPPResultImpl<Rule6> {
public:
  Rule6Result() {}
  virtual raw_ostream &print(raw_ostream &OS) const override {
    OS << "Rule6 not implemented\n";
    return OS;
  }
};

class Rule6Local : public DPPLocalRule {
public:
  Rule6Local() = delete;
  Rule6Local(DPPLocalAnalysis *DPPLA) : DPPLocalRule(DPPLA) {}

  virtual std::shared_ptr<DPPLocalResult> runOnFunction(
      Function &F, AnalysisManager<Function> &FAM) override;
};

std::shared_ptr<DPPLocalResult> Rule6Local::runOnFunction(Function &F,
                                          AnalysisManager<Function> &FAM) {
  return std::make_shared<Rule6Result>();
}


class Rule6Global : public DPPGlobalRule {
public:
  Rule6Global() = delete;
  Rule6Global(DPPGlobalAnalysis *DPPGA) : DPPGlobalRule(DPPGA) {}

  virtual std::shared_ptr<DPPLocalResult> runOnModule(
      llvm::Module &M, llvm::AnalysisManager<llvm::Module> &MAM) override;
};

std::shared_ptr<DPPLocalResult>
Rule6Global::runOnModule(Module &M, AnalysisManager<llvm::Module> &MAM) {
  for (auto &F : M) {
    if (F.isDeclaration()) // Skip undefined functions
      continue;
    // TODO: Do something here...
    // FAM.getResult<DPPLocalAnalysis>(F));
  }
  return std::make_shared<Rule6Result>();
}

} // namespace

DPPLocalRule *DPP::createLocalRule6(DPPLocalAnalysis *DPPLA) {
  return new Rule6Local(DPPLA);
}

DPPGlobalRule *DPP::createGlobalRule6(DPPGlobalAnalysis *DPPGA) {
  return new Rule6Global(DPPGA);
}
