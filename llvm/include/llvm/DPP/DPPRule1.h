//
// Created by salman on 3/8/21.
//

#ifndef DPP_DPPRULE1_H
#define DPP_DPPRULE1_H

#include "llvm/DPP/DPPRule.h"

namespace SVF {
class PAG;
class PTACallGraph;
class SVFG;
} // namespace SVF

using namespace llvm;
using namespace std;
using namespace SVF;


#define BACKWARD_CMPS 2
#define BACKWARD_CMP_PATHS 2

#define NO_CMP -3
#define YES_CMP 1
#define HAVE_NONE_BOUND -2
#define HAVE_CONSTANT_BOUND 1
#define HAVE_VARIABLE_BOUND -2
#define POTENTIALLY_UNSAFE_USE -1
#define POTENTIALLY_SAFE_USE 1

typedef struct DPPScore_t {
    unsigned long long ID;
    signed int Score;
} DPPScore;

enum BoundApproxType {
    NONE = 0,
    VARIABLE,
    CONSTANT
};

typedef struct InputFunction_t {
    StringRef name;
    unsigned int inputArgStart;
    bool considerRet;
} InputFunction;

using InputFunctionMap = DenseMap<StringRef, InputFunction *>;

void Rule1Global(PAG *pag, PTACallGraph* callgraph, SVFG *svfg);


namespace llvm {
namespace DPP {

class DPPRule1G;
class DPPRule1GResult;

class DPPRule1G : public AnalysisInfoMixin<DPPRule1G> {
  friend AnalysisInfoMixin<DPPRule1G>;
public:
  using Result = DPPRule1GResult;

  static const char RuleName[];
  static AnalysisKey Key;

  Result run(Module &M, AnalysisManager<Module> &AM);
};

class DPPRule1GResult : public DPPResult<DPPRule1G> {
  friend DPPRule1G;
public:
private:
public:
  DPPRule1GResult() {}
  raw_ostream &print(raw_ostream &OS) const;
};

class [[maybe_unused]] DPPRule1GPrinterPass
    : public DPPGlobalPrinterPass<DPPRule1G> {
public:
  [[maybe_unused]] DPPRule1GPrinterPass(raw_ostream &OS)
      : DPPGlobalPrinterPass(OS) {}
};

} // namespace DPP
} // namespace llvm

#endif //DPP_DPPRULE1_H
