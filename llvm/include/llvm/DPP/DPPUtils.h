//
// Created by salman on 7/18/21.
//

#ifndef DPP_LLVM_DPPUTILS_H
#define DPP_LLVM_DPPUTILS_H

#include <string>

#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/ADT/DenseSet.h"



namespace SVF {
    class SVFG;
    class VFGNode;
    class PAG;
    class PAGNode;
} // namespace SVF

namespace llvm {
namespace DPP {

enum DpiType {
    DpiNone,
    DpiFull,
    DpiDpp
};

using ValSet = DenseSet<const Value *>;
using ValUserMap = DenseMap<const Value *, ValSet >;
using DPPMap = DenseMap<const Value *, int32_t>;

//+++++++++++++++ uses SVF functionalities ++++++++++++++++++++++++++
ValSet GetDataPointerInstructions(SVF::SVFG *svfg, bool needTotal);

bool useDpi();
bool isUnionTypePunningSupported(void);
void setDppDpiUnionTypePunning(bool value);

DpiType getDpiType();
std::string getRuleNum();
bool isLogIndividualRule();
int32_t getNumTopKDObjs();
std::string getInputFilename();
std::string getOutputFilename();
int32_t getNumBackwardCmps();
int32_t getNumBackwardCmpPaths();
int32_t getNumNodesInPath();

bool isDataPointer(const Type *const T);

DenseSet<std::string *> getInputLibFunctions();
DenseSet<std::string *> getVulnLibFunctions();
void writeDPPLogsToFile(std::string data);


//++++++++++++++++ LLVM related ++++++++++++++++++++++
bool isConstantGetElemInst(const Value *V);

} // namespace DPP
} // namespace llvm

#endif //DPP_LLVM_DPPUTILS_H
