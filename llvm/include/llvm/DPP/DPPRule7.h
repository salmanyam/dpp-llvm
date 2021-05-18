//
// Created by salman on 3/8/21.
//

#ifndef DPP_DPPRULE7_H
#define DPP_DPPRULE7_H

#include "llvm/DPP/DPPRule.h"

namespace SVF {
class PAG;
class PTACallGraph;
class SVFG;
} // namespace SVF

using namespace llvm;
using namespace std;
using namespace SVF;

typedef struct InputFunction_t {
    StringRef name;
    unsigned int inputArgStart;
    bool considerRet;
} InputFunction;

using InputFunctionMap = DenseMap<StringRef, InputFunction *>;

void Rule7Global(PAG *pag, PTACallGraph* callgraph, SVFG *svfg);


namespace llvm {
namespace DPP {

class DPPRule7G;
class DPPRule7GResult;

class DPPRule7G : public AnalysisInfoMixin<DPPRule7G> {
  friend AnalysisInfoMixin<DPPRule7G>;
public:
  using Result = DPPRule7GResult;

  static const char RuleName[];
  static AnalysisKey Key;

  Result run(Module &M, AnalysisManager<Module> &AM);
};

class DPPRule7GResult : public DPPResult<DPPRule7G> {
  friend DPPRule7G;
public:
private:
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

#endif //DPP_DPPRULE7_H
