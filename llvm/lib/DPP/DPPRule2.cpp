//
// Created by salman on 7/1/21.
//

#include "llvm/DPP/SVFInitPass.h"
#include "llvm/DPP/DPPRule1.h"
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
        //errs() << "Ptr = " << *Ptr << "\n";
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
    //PTACallGraph *Callgraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    LLVM_DEBUG(dbgs() << "Running rule 2...\n");

    //auto DPValues = DPP::GetDataPointerInstructions(svfg, false);
    auto TaintedObjects = AM.getResult<DPPRule1G>(M);

    /// store the users of a value to a map
    ValUserMap VUMap;
    for (auto Item: TaintedObjects.PrioritizedPtrMap) { // previously here was DPValues
        /// DP users list also include itself (DPInst) as a user
        auto DPUsers = GetCompleteUsers(Item.getFirst(), svfg); // previously was only Item
        VUMap.try_emplace(Item.getFirst(), DPUsers);
    }

    /*for (auto Item: VUMap) {
        errs() << "Value = " << *Item.getFirst() << "\n-----------------------------\n";
        for (auto V: Item.getSecond()) {
            errs() << "User = " << *V << "\n";
        }
    }*/

    LLVM_DEBUG(dbgs() << "Checking the existence of data pointer in conditions...\n");

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
    if (DPP::isLogIndividualRule())
        DPP::writeDPPLogsToFile(dppLog);

    for (auto Item: AlreadyCovered) {
        Result.PrioritizedPtrMap.try_emplace(Item, 1);
    }

    errs() << "Rule2 done...\n";

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
