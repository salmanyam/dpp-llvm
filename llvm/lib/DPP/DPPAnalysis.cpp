//==- DPPGlobalAnalysis.cpp ------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include <utility>

#include "llvm/DPP/DPPAnalysis.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

#define DEBUG_TYPE "DPPGlobalAnalysis"

using namespace llvm;
using namespace llvm::DPP;
using namespace SVF;

AnalysisKey DPPAnalysis::Key;

const VFGNode* DPPAnalysis::getVFGNodeFromValue(PAG *pag, SVFG *svfg, const Value *val) {
    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(val));
    const VFGNode* vNode = svfg->getDefSVFGNode(pNode);
    return vNode;
}

ValSet DPPAnalysis::GetCompleteUsers(const Value *Val, SVFG *svfg) {
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

/// get all the pointers pointing to the object pointed by Val
ValSet DPPAnalysis::getPointersToObject(const Value *Val, SVFG *svfg) {
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

std::pair<ValSet, uint32_t> DPPAnalysis::getTotalDataPointers(SVFG *svfg) {
    ValSet totalDataPointers;
    auto DPValues = DPP::GetDataPointerInstructions(svfg, true);
    for (auto DPVal: DPValues) {
        //auto Pointers = getPointersToObject(DPVal, svfg);
        auto Pointers = GetCompleteUsers(DPVal, svfg);
        for (auto Item: Pointers) {
            if (const auto *I = dyn_cast<Instruction>(Item)) {
                if (I->getOpcode() == Instruction::Load || I->getOpcode() == Instruction::Store) {
                    totalDataPointers.insert(Item);
                }
            }
        }
    }
    return std::make_pair(totalDataPointers, DPValues.size());
}

ValSet DPPAnalysis::getDataPointersToObjects(DPPMap Map, SVFG *svfg) {
    ValSet totalDataPointers;
    for (auto Item : Map) {
        auto *const Val = Item.getFirst();
        //auto Pointers = getPointersToObject(Val, svfg);
        auto Pointers = GetCompleteUsers(Val, svfg);
        for (auto Ptr: Pointers) {
            if (const auto *I = dyn_cast<Instruction>(Ptr)) {
                if (I->getOpcode() == Instruction::Load || I->getOpcode() == Instruction::Store) {
                    totalDataPointers.insert(Ptr);
                }
            }
        }
    }
    return totalDataPointers;
}

ValSet DPPAnalysis::getDataObjects(DPPMap Map) {
    ValSet totalDataObjects;
    for (auto Item : Map) {
        auto *const Val = Item.getFirst();
        totalDataObjects.insert(Val);
    }
    return totalDataObjects;
}

DPPMap DPPAnalysis::filterObjects(DPPMap Map1, DPPMap Map2) {
    DPPMap R;
    for (auto Item : Map1) {
        auto *const Val = Item.getFirst();
        if (Map2.find(Val) == Map2.end())
            continue;

        R.try_emplace(Val, Item.getSecond());
    }

    return R;
}

DPPAnalysis::Result DPPAnalysis::run(Module &M, AnalysisManager<Module> &AM) {
    LLVM_DEBUG(dbgs() << "DPPGlobalAnalysis::run starting up...\n");

    auto R = AM.getResult<SVFInitPass>(M);

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *CallGraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    LLVM_DEBUG(dbgs() << "Counting total data pointers\n");

    auto objPtrPair = getTotalDataPointers(svfg);
    auto totalDataPointers = objPtrPair.first; // set of data pointers
    auto totalDataObjects = objPtrPair.second; // number of data objects


    auto ruleNum = DPP::getRuleNum();

    if (ruleNum.compare("rule1") == 0) {
        auto Rule1Result = AM.getResult<DPPRule1G>(M);
        auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);
        //auto Rule1Objects = getDataObjects(Rule1Result.PrioritizedPtrMap);

        totalDataPointers.insert(Rule1Set.begin(), Rule1Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule1Set.size() << "(" << Rule1Result.PrioritizedPtrMap.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule2") == 0) {
        auto Rule2Result = AM.getResult<DPPRule2G>(M);
        auto Rule2Set = getDataPointersToObjects(Rule2Result.PrioritizedPtrMap, svfg);
        //auto Rule2Objects = getDataObjects(Rule2Result.PrioritizedPtrMap);

        totalDataPointers.insert(Rule2Set.begin(), Rule2Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule2Set.size() << "(" << Rule2Result.PrioritizedPtrMap.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule3") == 0) {
        auto Rule3Result = AM.getResult<DPPRule3G>(M);
        auto Rule3Set = getDataPointersToObjects(Rule3Result.PrioritizedPtrMap, svfg);
        //auto Rule3Objects = getDataObjects(Rule3Result.PrioritizedPtrMap);

        totalDataPointers.insert(Rule3Set.begin(), Rule3Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule3Set.size() << "(" << Rule3Result.PrioritizedPtrMap.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule5") == 0) {
        auto Rule5Result = AM.getResult<DPPRule5G>(M);
        auto Rule5Set = getDataPointersToObjects(Rule5Result.PrioritizedPtrMap, svfg);
        //auto Rule5Objects = getDataObjects(Rule5Result.PrioritizedPtrMap);

        totalDataPointers.insert(Rule5Set.begin(), Rule5Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule5Set.size() << "(" << Rule5Result.PrioritizedPtrMap.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule6") == 0) {
        auto Rule6Result = AM.getResult<DPPRule6G>(M);
        auto Rule6Set = getDataPointersToObjects(Rule6Result.PrioritizedPtrMap, svfg);
        //auto Rule6Objects = getDataObjects(Rule6Result.PrioritizedPtrMap);

        totalDataPointers.insert(Rule6Set.begin(), Rule6Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule6Set.size() << "(" << Rule6Result.PrioritizedPtrMap.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule7") == 0) {
        auto Rule7Result = AM.getResult<DPPRule7G>(M);
        auto Rule7Set = getDataPointersToObjects(Rule7Result.PrioritizedPtrMap, svfg);
        //auto Rule7Objects = getDataObjects(Rule7Result.PrioritizedPtrMap);

        totalDataPointers.insert(Rule7Set.begin(), Rule7Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule7Set.size() << "(" << Rule7Result.PrioritizedPtrMap.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule8") == 0) {
        auto Rule1Result = AM.getResult<DPPRule1G>(M);
        auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);

        auto Rule8Result = AM.getResult<DPPRule8G>(M);
        /// filter rule 8 by considering buffers that are dependent on user, i.e., tainted
        auto FilteredRule8Result = filterObjects(Rule8Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);

        /// write some logs to file
        string dppLog = "#################### RULE 8 #########################\n";
        for (auto Item: FilteredRule8Result) {
            auto SVFNode = getVFGNodeFromValue(pag, svfg, Item.getFirst());
            dppLog += SVFNode->toString() + "\n";
            dppLog += "--------------------------------------------------------------\n";
        }
        dppLog += "##################################################\n\n\n";
        DPP::writeDPPLogsToFile(dppLog);

        auto Rule8Set = getDataPointersToObjects(FilteredRule8Result, svfg);
        //auto Rule8Objects = getDataObjects(Rule8Result.PrioritizedPtrMap);

        totalDataPointers.insert(Rule8Set.begin(), Rule8Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule8Set.size() << "(" << FilteredRule8Result.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule9") == 0) {
        auto Rule9Result = AM.getResult<DPPRule9G>(M);
        auto Rule9Set = getDataPointersToObjects(Rule9Result.PrioritizedPtrMap, svfg);
        //auto Rule9Objects = getDataObjects(Rule9Result.PrioritizedPtrMap);

        totalDataPointers.insert(Rule9Set.begin(), Rule9Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule9Set.size() << "(" << Rule9Result.PrioritizedPtrMap.size() << ") " << "\n";
    }
    else {
        auto Rule1Result = AM.getResult<DPPRule1G>(M);
        auto Rule2Result = AM.getResult<DPPRule2G>(M);
        auto Rule3Result = AM.getResult<DPPRule3G>(M);
        auto Rule5Result = AM.getResult<DPPRule5G>(M);
        auto Rule6Result = AM.getResult<DPPRule6G>(M);
        auto Rule7Result = AM.getResult<DPPRule7G>(M);
        auto Rule8Result = AM.getResult<DPPRule8G>(M);
        auto Rule9Result = AM.getResult<DPPRule9G>(M);

        /// filter rule 8 by considering buffers that are dependent on user, i.e., tainted
        auto FilteredRule8Result = filterObjects(Rule8Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);
        //errs() << "Before size = " << Rule8Result.PrioritizedPtrMap.size() << " After = " << FilteredRule8Result.size() << "\n";

        /// write rule 8 logs to file
        string dppLog = "#################### RULE 8 #########################\n";
        for (auto Item: FilteredRule8Result) {
            auto SVFNode = getVFGNodeFromValue(pag, svfg, Item.getFirst());
            dppLog += SVFNode->toString() + "\n";
            dppLog += "--------------------------------------------------------------\n";
        }
        dppLog += "##################################################\n\n\n";
        DPP::writeDPPLogsToFile(dppLog);

        auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);
        auto Rule2Set = getDataPointersToObjects(Rule2Result.PrioritizedPtrMap, svfg);
        auto Rule3Set = getDataPointersToObjects(Rule3Result.PrioritizedPtrMap, svfg);
        auto Rule5Set = getDataPointersToObjects(Rule5Result.PrioritizedPtrMap, svfg);
        auto Rule6Set = getDataPointersToObjects(Rule6Result.PrioritizedPtrMap, svfg);
        auto Rule7Set = getDataPointersToObjects(Rule7Result.PrioritizedPtrMap, svfg);
        auto Rule8Set = getDataPointersToObjects(FilteredRule8Result, svfg);
        auto Rule9Set = getDataPointersToObjects(Rule9Result.PrioritizedPtrMap, svfg);

        auto Rule1Objects = getDataObjects(Rule1Result.PrioritizedPtrMap);
        auto Rule2Objects = getDataObjects(Rule2Result.PrioritizedPtrMap);
        auto Rule3Objects = getDataObjects(Rule3Result.PrioritizedPtrMap);
        auto Rule5Objects = getDataObjects(Rule5Result.PrioritizedPtrMap);
        auto Rule6Objects = getDataObjects(Rule6Result.PrioritizedPtrMap);
        auto Rule7Objects = getDataObjects(Rule7Result.PrioritizedPtrMap);
        auto Rule8Objects = getDataObjects(FilteredRule8Result);
        auto Rule9Objects = getDataObjects(Rule9Result.PrioritizedPtrMap);


        ValSet CombinedSet;
        CombinedSet.insert(Rule1Set.begin(), Rule1Set.end());
        CombinedSet.insert(Rule2Set.begin(), Rule2Set.end());
        CombinedSet.insert(Rule3Set.begin(), Rule3Set.end());
        CombinedSet.insert(Rule5Set.begin(), Rule5Set.end());
        CombinedSet.insert(Rule6Set.begin(), Rule6Set.end());
        CombinedSet.insert(Rule7Set.begin(), Rule7Set.end());
        CombinedSet.insert(Rule8Set.begin(), Rule8Set.end());
        CombinedSet.insert(Rule9Set.begin(), Rule9Set.end());

        ValSet CombinedObjects;
        CombinedObjects.insert(Rule1Objects.begin(), Rule1Objects.end());
        CombinedObjects.insert(Rule2Objects.begin(), Rule2Objects.end());
        CombinedObjects.insert(Rule3Objects.begin(), Rule3Objects.end());
        CombinedObjects.insert(Rule5Objects.begin(), Rule5Objects.end());
        CombinedObjects.insert(Rule6Objects.begin(), Rule6Objects.end());
        CombinedObjects.insert(Rule7Objects.begin(), Rule7Objects.end());
        CombinedObjects.insert(Rule8Objects.begin(), Rule8Objects.end());
        CombinedObjects.insert(Rule9Objects.begin(), Rule9Objects.end());


        // in case, tht total pointer calculation missed some pointers
        totalDataPointers.insert(CombinedSet.begin(), CombinedSet.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule1Set.size() << "(" << Rule1Result.PrioritizedPtrMap.size() << ") "
        << Rule2Set.size() << "(" << Rule2Result.PrioritizedPtrMap.size() << ") "
        << Rule3Set.size() << "(" << Rule3Result.PrioritizedPtrMap.size() << ") "
        << "-" << " "
        << Rule5Set.size() << "(" << Rule5Result.PrioritizedPtrMap.size() << ") "
        << Rule6Set.size() << "(" << Rule6Result.PrioritizedPtrMap.size() << ") "
        << Rule7Set.size() << "(" << Rule7Result.PrioritizedPtrMap.size() << ") "
        << Rule8Set.size() << "(" << FilteredRule8Result.size() << ") "
        << Rule9Set.size() << "(" << Rule9Result.PrioritizedPtrMap.size() << ") "
        << CombinedSet.size() << "(" << CombinedObjects.size() << ") " << "\n";
    }

    return DPPAnalysis::Result("not implemented\n");
}

PreservedAnalyses DPPAnalysisPrinterPass::run(Module &M, AnalysisManager<Module> &AM) {
  OS << "Data Pointer Prioritization Analysis\n";

  auto Results = AM.getResult<DPPAnalysis>(M);
  Results.print(OS);

  return PreservedAnalyses::all();
}
