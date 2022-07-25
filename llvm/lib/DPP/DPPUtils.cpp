//
// Created by salman on 7/18/21.
//

#include <fstream>

#include "llvm/ADT/DenseSet.h"
#include "llvm/DPP/DPPUtils.h"
#include "llvm/Support/CommandLine.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

#define DEBUG_TYPE "DPPUtils"

using namespace llvm;
using namespace llvm::DPP;
using namespace SVF;
using namespace std;

static cl::opt<string> DppRuleNum("dpp-rule", cl::init("all"),
                                     cl::desc("Specify the rule number <rule x>"),
                                     cl::value_desc("rule number"));

static cl::opt<int32_t> DppTopK("dpp-topk", cl::init(100),
                                         cl::desc("Specify the number of top-k data objects"),
                                         cl::value_desc("number"));

static cl::opt<bool> DppIsLogIndividualRule("dpp-log-rule", cl::Hidden,
                                  cl::desc("DPP log individual rule"),
                                  cl::init(false));

static cl::opt<string> InputFileLibFunctions("dpp-input", cl::init("/home/salman/DPP/dpp-llvm/dpp/data/functions.txt"),
                                             cl::desc("Specify the file for DPP related input"),
                                             cl::value_desc("filename"));

static cl::opt<string> DppOutputFile("dpp-output", cl::init("/home/salman/DPP/dpp-llvm/dpp/data/dpp-logs.txt"),
                                     cl::desc("Specify the output file for DPP logs"),
                                     cl::value_desc("filename"));

static cl::opt<DpiType> DppDpiType("dpi-type", cl::init(DpiNone),
        cl::desc("Data-pointer prioritization type"),
        cl::value_desc("mode"),
        cl::values(clEnumValN(DpiNone, "none", "No data-pointer protection"),
                   clEnumValN(DpiFull, "full", "Data-pointer protection"),
                   clEnumValN(DpiDpp, "dpp", "Data-pointer protection with prioritization")));

static cl::opt<bool> EnableDppDpi("dpp-dpi", cl::Hidden,
                                    cl::desc("DPP backward-edge CFI"),
                                    cl::init(false));

static cl::opt<bool> EnableDppDpiUnionTypePunning("dpp-dpi-union-type-punning", cl::Hidden,
                                                    cl::desc("Disable DPP when loading union data members"),
                                                    cl::init(false));

static cl::opt<int32_t> BackwardCmps("dpp-num-backward-cmps", cl::init(2),
                                     cl::desc("Specify the number of backward cmps to consider for bound checking"),
                                     cl::value_desc("number"));

static cl::opt<int32_t> BackwardCmpPaths("dpp-num-backward-cmp-paths", cl::init(2),
                                         cl::desc("Specify the number of backward cmp paths to consider for bound checking"),
                                         cl::value_desc("number"));

static cl::opt<int32_t> NodeInPaths("dpp-num-nodes-in-path", cl::init(50),
                                         cl::desc("Specify the number of nodes in a path to get backward paths"),
                                         cl::value_desc("number"));


void llvm::DPP::setDppDpiUnionTypePunning(bool value) {
    EnableDppDpiUnionTypePunning = value;
}

bool llvm::DPP::useDpi() {
    return EnableDppDpi || (DppDpiType != DpiNone);
}

bool llvm::DPP::isUnionTypePunningSupported() {
    return EnableDppDpiUnionTypePunning;
}

llvm::DPP::DpiType DPP::getDpiType() {
    return DppDpiType;
}

int32_t llvm::DPP::getNumTopKDObjs() {
    return DppTopK;
}

bool llvm::DPP::isLogIndividualRule() {
    return DppIsLogIndividualRule;
}

string llvm::DPP::getRuleNum() {
    return DppRuleNum;
}

string llvm::DPP::getInputFilename() {
    return InputFileLibFunctions;
}

string llvm::DPP::getOutputFilename() {
    return DppOutputFile;
}

int32_t llvm::DPP::getNumBackwardCmps() {
    return BackwardCmps;
}

int32_t llvm::DPP::getNumBackwardCmpPaths() {
    return BackwardCmpPaths;
}

int32_t llvm::DPP::getNumNodesInPath() {
    return NodeInPaths;
}

bool llvm::DPP::isDataPointer(const Type *const T) {
    if (T && T->isPointerTy() && !T->isFunctionTy())
        return true;

    if (T && T->isArrayTy() && !T->isFunctionTy())
        return true;

    return false;
}

DenseSet<string *> llvm::DPP::getInputLibFunctions() {
    DenseSet<string *> IF;
    string line;
    ifstream inputFile (DPP::getInputFilename());
    string delimiter = " ";
    if (inputFile.is_open()) {
        while (getline(inputFile, line)) {
            auto pos = line.find(delimiter);
            string type = line.substr(0, pos);

            line.erase(0, pos + delimiter.length());

            string *func = new string(line);
            LLVM_DEBUG(dbgs() << "Reading function name = " << line << "\n");
            if (type.compare("IF") == 0)
                IF.insert(func);
        }
        inputFile.close();
    }
    else {
        LLVM_DEBUG(dbgs() << "Unable to open file\n");
    }

    return IF;
}

DenseSet<string *> llvm::DPP::getVulnLibFunctions() {
    DenseSet<string *> VF;
    string line;
    ifstream inputFile (DPP::getInputFilename());
    string delimiter = " ";
    if (inputFile.is_open()) {
        while (getline(inputFile, line)) {
            auto pos = line.find(delimiter);
            string type = line.substr(0, pos);

            line.erase(0, pos + delimiter.length());

            string *func = new string(line);
            LLVM_DEBUG(dbgs() << "Reading function name = " << line << "\n");
            if (type.compare("VULN") == 0)
                VF.insert(func);
        }
        inputFile.close();
    }
    else {
        LLVM_DEBUG(dbgs() << "Unable to open file\n");
    }

    return VF;
}

void llvm::DPP::writeDPPLogsToFile(string data) {
    std::ofstream outfile;
    auto filename = DPP::getOutputFilename();
    outfile.open(filename, std::ios_base::app); // append instead of overwrite
    outfile << data;
    outfile.close();
}

/// return the instructions where address taken objects are created, e.g., alloca or malloc, etc.
ValSet llvm::DPP::GetDataPointerInstructions(SVFG *svfg, bool needTotal) {
    ValSet Insts;

    /// for each address taken nodes
    for (auto SV = svfg->begin(); SV != svfg->end(); ++SV) {
        VFGNode *V = SV->second;
        if (auto *AVF = SVFUtil::dyn_cast<AddrVFGNode>(V)) { // address vfg node
            Type *PTy = nullptr;

            if (const auto *I = SVFUtil::dyn_cast<Instruction>(AVF->getInst())) {
                if (I->getOpcode() == Instruction::Alloca) {
                    if (const auto *AI = SVFUtil::dyn_cast<AllocaInst>(I)) {
                        PTy = AI->getAllocatedType();
                    }
                } else if (I->getOpcode() == Instruction::Call) {
                    if (const auto *CI = SVFUtil::dyn_cast<CallInst>(I)) {
                        PTy = CI->getCalledFunction()->getReturnType();
                    }
                }

            } else {
                /// add the global variable (may be) here
                if (AVF->getPAGSrcNode()->hasValue()) {
                    if (auto GlobalVar = llvm::dyn_cast<GlobalVariable>(AVF->getPAGSrcNode()->getValue())) {
                        if (DPP::isDataPointer(GlobalVar->getType()->getPointerElementType())) {
                            Insts.insert(GlobalVar);
                        }
                    }
                }

            }

            if (PTy && DPP::isDataPointer(PTy)) {
                if (const auto *I = SVFUtil::dyn_cast<Instruction>(AVF->getInst())) {
                    // for counting the total objects
                    if (needTotal) {
                        Insts.insert(I);

                    }else {
                        /// discard objects with constant operands
                        bool hasVarOperand = false;
                        for (auto Op = I->op_begin(); Op != I->op_end(); ++Op) {
                            if (llvm::isa<Constant>(Op->get()))
                                continue;

                            if (isConstantGetElemInst(Op->get()))
                                continue;

                            hasVarOperand = true;
                        }

                        if (hasVarOperand)
                            Insts.insert(I);
                    }
                }
            }
        }
    }

    return Insts;
}


bool llvm::DPP::isConstantGetElemInst(const Value *V) {
    /// convert the value to a get element instruction and check all its operand to see if they are constant
    /// return false if any operand is not constant, otherwise return true
    if (const Instruction *I = llvm::dyn_cast<Instruction>(V)) {
        if (I->getOpcode() == Instruction::GetElementPtr) {
            for (auto Op = I->op_begin(); Op != I->op_end(); ++Op) {
                if (!llvm::isa<Constant>(Op->get()))
                    return false;
            }
            return true; // because we know get elem has two or three params and all of them are constant
        }
    }
    return false;
}