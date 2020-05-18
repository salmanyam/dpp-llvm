//
// Created by ishkamiel on 18/05/2020.
//

#ifndef LLVM_DPP_H
#define LLVM_DPP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace DPP {

enum RuleType { Rule6, Rule7 };

class DPPLocalResult;
class DPPLocalRule;
class DPPGlobalRule;
class DPPLocalAnalysis;
class DPPGlobalAnalysis;
using DPPResultMap = llvm::DenseMap<int,std::shared_ptr<DPPLocalResult>>;

DPPLocalRule *createLocalRule6(DPPLocalAnalysis *);

DPPGlobalRule *createGlobalRule6(DPPGlobalAnalysis *);

/// Interface for interacting with Rule results
class DPPLocalResult {
public:
  //virtual DPPLocalResult();
  virtual ~DPPLocalResult() {};
  virtual llvm::raw_ostream &print(llvm::raw_ostream &OS) const = 0;
  virtual bool isType(RuleType RuleType) const  = 0;
  virtual RuleType getType() const = 0;
};

/// Base implementation class for Rule results
template<RuleType T>
class DPPResultImpl : public DPPLocalResult {
  bool isType(RuleType RuleType) const final { return RuleType == T; }
  RuleType getType() const final { return T; }
};

/// Base class for local Rule implementations
class DPPLocalRule {
protected:
  DPPLocalAnalysis *DPPLA;

public:
  DPPLocalRule() = delete;
  explicit DPPLocalRule(DPPLocalAnalysis *DPPLA) : DPPLA(DPPLA) {}
  virtual ~DPPLocalRule() {}

  virtual std::shared_ptr<DPPLocalResult> runOnFunction(
      llvm::Function &F, llvm::AnalysisManager<llvm::Function> &FAM) = 0;
};

/// Base class for global rule implementations
class DPPGlobalRule {
protected:
  DPPGlobalAnalysis *DPPGA;

public:
  DPPGlobalRule() = delete;
  explicit DPPGlobalRule(DPPGlobalAnalysis *DPPGA) : DPPGA(DPPGA) {}
  virtual ~DPPGlobalRule() {}

  virtual std::shared_ptr<DPPLocalResult> runOnModule(
      llvm::Module &M, llvm::AnalysisManager<llvm::Module> &MAM) = 0;
};

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                     const DPPLocalResult &Result)
{ Result.print(OS); return OS; }

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                     const DPPLocalResult *Result)
{ Result->print(OS); return OS; }

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                     const std::shared_ptr<DPPLocalResult> Result)
{ Result->print(OS); return OS; }

} // namespace DPP

#endif // LLVM_DPP_H
