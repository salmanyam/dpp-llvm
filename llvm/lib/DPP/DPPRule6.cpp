//
// Created by salman on 7/5/21.
//

#include "llvm/DPP/SVFInitPass.h"
#include "llvm/DPP/DPPRule6.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

#define DEBUG_TYPE "DPPRule6"

using namespace llvm;
using namespace llvm::DPP;
using namespace SVF;

[[maybe_unused]] const char DPPRule6G::RuleName[] = "DPPRule6G";
AnalysisKey DPPRule6G::Key;

const VFGNode* DPPRule6G::getVFGNodeFromValue(PAG *pag, SVFG *svfg, const Value *val) {
    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(val));
    const VFGNode* vNode = svfg->getDefSVFGNode(pNode);
    return vNode;
}

DenseSet<StringRef> DPPRule6G::GetBlackListFunctions() {
    DenseSet<StringRef> blackListedFunctions;

    auto blFunctions = DPP::getVulnLibFunctions();
    for (auto Func: blFunctions) {
        //errs() << "Function = " << StringRef(Func->c_str());
        blackListedFunctions.insert(StringRef(Func->c_str()));
    }

    return blackListedFunctions;
}

ValSet DPPRule6G::getPointersToObject(const Value *Val, SVFG *svfg) {
    /// get all the pointers pointing to an object, i.e., the object pointed by Val
    ValSet Pointers;
    NodeID pNodeId = svfg->getPAG()->getObjectNode(Val);
    const NodeBS& pts = svfg->getPTA()->getRevPts(pNodeId);
    for (unsigned int pt : pts)
    {
        if (!svfg->getPAG()->hasGNode(pt))
            continue;

        PAGNode* targetPtr = svfg->getPAG()->getPAGNode(pt);
        if(targetPtr->hasValue())
        {
            //errs() << *targetPtr->getValue() << "\n";
            Pointers.insert(targetPtr->getValue());
        }
    }
    return Pointers;
}

ValSet DPPRule6G::GetCompleteUsers(const Value *Val, SVFG *svfg) {
    FIFOWorkList<const Value *> worklist;
    ValSet visited;

    /// insert the Val in the worklist as we need to check its existence as well
    worklist.push(Val);

    /// get all the pointers pointing to an object, i.e., the object pointed by Val
    auto Pointers = getPointersToObject(Val, svfg);
    for (auto Ptr: Pointers) {
        if (visited.find(Ptr) == visited.end())
            worklist.push(Ptr);
    }

    /// Traverse along all uses of Val
    while (!worklist.empty()) {
        const Value *V = worklist.pop();
        //errs() << "POPPING: " << *V << "\n";
        visited.insert(V);

        for (const auto U : V->users()) {
            //errs() << *U << "\n";
            if (visited.find(U) == visited.end())
                worklist.push(U);
        }
    }

    return visited;
}

DPPRule6G::Result DPPRule6G::run(Module &M, AnalysisManager<Module> &AM) {
    Result Result {};

    auto R = AM.getResult<SVFInitPass>(M);

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *CallGraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    auto DPValues = GetDataPointerInstructions(svfg, false);

    /// store the users of a value to a map
    ValUserMap VUMap;
    for (auto DPVal: DPValues) {
        /// DPusers list also include DPInst as a user
        auto DPUsers = GetCompleteUsers(DPVal, svfg);
        VUMap.try_emplace(DPVal, DPUsers);
    }

    /*
    errs() << "Printing value and its users\n";
    for (auto Item: VUMap) {
        auto Users = Item.getSecond();
        errs() << "\nValue: " << *Item.getFirst() << "\n";
        errs() << "---------------------------\n";
        for (auto User: Users) {
            errs() << "User: " << *User << "\n";
        }
    }
    errs() << "Printing end\n";
    */

    auto BLFunctions = GetBlackListFunctions();

    /// write some logs to file
    string dppLog = "#################### RULE 6 #########################\n";

    ValSet AlreadyCovered;

    /// Get all call sites and get the black-listed functions
    for(const CallBlockNode *CS: pag->getCallSiteSet()) {
        /// get the names of the calle functions to filter input functions
        PTACallGraph::FunctionSet callees;
        CallGraph->getCallees(CS,callees);
        for(auto func : callees) {
            //errs() << func->getName() << "\n";

            if ( BLFunctions.find(func->getName()) != BLFunctions.end()) {
                //errs() << func->getName() << "\n";
                unsigned int paramIndex = 0;
                for (auto P: CS->getActualParms()) {

                    //errs() << "Param " << paramIndex << ": " << *P << "\n";
                    paramIndex++;

                    /// skip constant type parameter
                    if(SVFUtil::isa<Constant>(P->getValue())) {
                        continue;
                    }

                    /// skip a variable that is deduced from constants, e.g., getelemetptr str_0, 0, 0
                    if (!DPP::hasVariableOperand(P->getValue())) {
                        continue;
                    }

                    /// check the existence of the parameter in a value's user list
                    /// loop through the data pointer values and find the parameter in the user list
                    for (auto Item: VUMap) {
                        //skip if already there
                        if (AlreadyCovered.find(Item.getFirst()) != AlreadyCovered.end())
                            continue;

                        auto Users = Item.getSecond();
                        if (Users.find(P->getValue()) != Users.end()) {
                            AlreadyCovered.insert(Item.getFirst());

                            auto SVFNode = getVFGNodeFromValue(pag, svfg, Item.getFirst());
                            dppLog += SVFNode->toString() + "\n";
                            dppLog += "--------------------------------------------------------------\n";
                        }
                    }

                }
                //errs() << "----------------------------------------------------\n";
            }
        }
        // errs() << "--------------------\n";
    }

    dppLog += "##################################################\n\n\n";
    DPP::writeDPPLogsToFile(dppLog);

    for (auto Item: AlreadyCovered) {
        Result.PrioritizedPtrMap.try_emplace(Item, 1);
    }

    return Result;
}


raw_ostream &DPPRule6GResult::print(raw_ostream &OS) const {
    OS << "Prioritized data objects by Rule 6:\n";
    for (auto Ptr : PrioritizedPtrMap) {
        auto *const I = Ptr.getFirst();
        auto score = Ptr.getSecond();
        OS << *I << ", score = " << score << "\n";
    }
    return OS;
}

