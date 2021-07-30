//
// Created by salman on 7/1/21.
//

#include "llvm/DPP/SVFInitPass.h"
#include "llvm/DPP/DPPRule2.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

#define DEBUG_TYPE "DPPRule2"

using namespace llvm;
using namespace llvm::DPP;
using namespace SVF;


[[maybe_unused]] const char DPPRule2G::RuleName[] = "DPPRule2G";
AnalysisKey DPPRule2G::Key;

const VFGNode* DPPRule2G::getVFGNodeFromValue(PAG *pag, SVFG *svfg, const Value *val) {
    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(val));
    const VFGNode* vNode = svfg->getDefSVFGNode(pNode);
    return vNode;
}

ValSet DPPRule2G::getPointersToObject(const Value *Val, SVFG *svfg) {
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

ValSet DPPRule2G::GetCompleteUsers(const Value *Val, SVFG *svfg) {
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

DPPRule2G::Result DPPRule2G::run(Module &M, AnalysisManager<Module> &AM) {
    Result Result {};

    auto R = AM.getResult<SVFInitPass>(M);

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *Callgraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    auto DPValues = DPP::GetDataPointerInstructions(svfg, false);

    /// store the users of a value to a map
    ValUserMap VUMap;
    for (auto DPVal: DPValues) {
        /// DPusers list also include DPInst as a user
        auto DPUsers = GetCompleteUsers(DPVal, svfg);
        VUMap.try_emplace(DPVal, DPUsers);
    }

    //errs() << "LLVM DEBUG flag = " << DebugFlag << "\n";

    /*errs() << "Printing value and its users\n";
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

    /// write some logs to file
    string dppLog = "#################### RULE 2 #########################\n";

    ValSet AlreadyCovered;
    /// check if the user list of a pointer has a compare instruction
    for (auto Item: VUMap) {
        auto Users = Item.getSecond();

        if (AlreadyCovered.find(Item.getFirst()) != AlreadyCovered.end())
            continue;

        for (auto User: Users) {
            if (const auto *I = SVFUtil::dyn_cast<Instruction>(User)) {
                auto Op = I->getOpcode();
                if (Op == Instruction::ICmp || Op == Instruction::FCmp) {
                    /// discard null pointer check
                    if (! isa<ConstantPointerNull>(I->getOperand(1))) {
                        AlreadyCovered.insert(Item.getFirst());
                        auto SVFNode = getVFGNodeFromValue(pag, svfg, Item.getFirst());
                        dppLog += SVFNode->toString() + "\n";
                        dppLog += "--------------------------------------------------------------\n";
                        break;
                    }
                }
            }
        }
    }

    dppLog += "##################################################\n\n\n";
    DPP::writeDPPLogsToFile(dppLog);

    for (auto Item: AlreadyCovered) {
        Result.PrioritizedPtrMap.try_emplace(Item, 1);
    }

    return Result;
}

raw_ostream &DPPRule2GResult::print(raw_ostream &OS) const {
    OS << "Prioritized data objects by Rule 2:\n";
    for (auto Ptr : PrioritizedPtrMap) {
        auto *const I = Ptr.getFirst();
        auto score = Ptr.getSecond();
        OS << *I << ", score = " << score << "\n";
    }
    return OS;
}
