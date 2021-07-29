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

ValSet DPPAnalysis::getPointersToObject(const Value *Val, SVFG *svfg) {
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

std::pair<ValSet, uint32_t> DPPAnalysis::getTotalDataPointers(SVFG *svfg) {
    ValSet totalDataPointers;
    auto DPValues = DPP::GetDataPointerInstructions(svfg, true);
    for (auto DPVal: DPValues) {
        auto Pointers = getPointersToObject(DPVal, svfg);
        for (auto Item: Pointers)
            totalDataPointers.insert(Item);
    }
    return std::make_pair(totalDataPointers, DPValues.size());
}

ValSet DPPAnalysis::getDataPointersToObjects(DPPMap Map, SVFG *svfg) {
    ValSet totalDataPointers;
    for (auto Item : Map) {
        auto *const Val = Item.getFirst();
        auto Pointers = getPointersToObject(Val, svfg);
        for (auto Ptr: Pointers)
            totalDataPointers.insert(Ptr);
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

DPPAnalysis::Result DPPAnalysis::run(Module &M, AnalysisManager<Module> &AM) {
    LLVM_DEBUG(dbgs() << "DPPGlobalAnalysis::run starting up...\n");

    auto R = AM.getResult<SVFInitPass>(M);

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *CallGraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    string result = "";

    auto ruleNum = DPP::getRuleNum();

    /*if (ruleNum.compare("rule1") == 0) {
        auto Rule1Result = AM.getResult<DPPRule1G>(M);
        auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);
        auto Rule1Objects = getDataObjects(Rule1Result.PrioritizedPtrMap);
    }*/

    auto Rule1Result = AM.getResult<DPPRule1G>(M);
    auto Rule2Result = AM.getResult<DPPRule2G>(M);
    auto Rule3Result = AM.getResult<DPPRule3G>(M);
    auto Rule5Result = AM.getResult<DPPRule5G>(M);
    auto Rule6Result = AM.getResult<DPPRule6G>(M);
    auto Rule7Result = AM.getResult<DPPRule7G>(M);
    auto Rule9Result = AM.getResult<DPPRule9G>(M);


    auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);
    auto Rule2Set = getDataPointersToObjects(Rule2Result.PrioritizedPtrMap, svfg);
    auto Rule3Set = getDataPointersToObjects(Rule3Result.PrioritizedPtrMap, svfg);
    auto Rule5Set = getDataPointersToObjects(Rule5Result.PrioritizedPtrMap, svfg);
    auto Rule6Set = getDataPointersToObjects(Rule6Result.PrioritizedPtrMap, svfg);
    auto Rule7Set = getDataPointersToObjects(Rule7Result.PrioritizedPtrMap, svfg);
    auto Rule9Set = getDataPointersToObjects(Rule9Result.PrioritizedPtrMap, svfg);

    auto Rule1Objects = getDataObjects(Rule1Result.PrioritizedPtrMap);
    auto Rule2Objects = getDataObjects(Rule2Result.PrioritizedPtrMap);
    auto Rule3Objects = getDataObjects(Rule3Result.PrioritizedPtrMap);
    auto Rule5Objects = getDataObjects(Rule5Result.PrioritizedPtrMap);
    auto Rule6Objects = getDataObjects(Rule6Result.PrioritizedPtrMap);
    auto Rule7Objects = getDataObjects(Rule7Result.PrioritizedPtrMap);
    auto Rule9Objects = getDataObjects(Rule9Result.PrioritizedPtrMap);


    ValSet CombinedSet;
    CombinedSet.insert(Rule1Set.begin(), Rule1Set.end());
    CombinedSet.insert(Rule2Set.begin(), Rule2Set.end());
    CombinedSet.insert(Rule3Set.begin(), Rule3Set.end());
    CombinedSet.insert(Rule5Set.begin(), Rule5Set.end());
    CombinedSet.insert(Rule6Set.begin(), Rule6Set.end());
    CombinedSet.insert(Rule7Set.begin(), Rule7Set.end());
    CombinedSet.insert(Rule9Set.begin(), Rule9Set.end());

    ValSet CombinedObjects;
    CombinedObjects.insert(Rule1Objects.begin(), Rule1Objects.end());
    CombinedObjects.insert(Rule2Objects.begin(), Rule2Objects.end());
    CombinedObjects.insert(Rule3Objects.begin(), Rule3Objects.end());
    CombinedObjects.insert(Rule5Objects.begin(), Rule5Objects.end());
    CombinedObjects.insert(Rule6Objects.begin(), Rule6Objects.end());
    CombinedObjects.insert(Rule7Objects.begin(), Rule7Objects.end());
    CombinedObjects.insert(Rule9Objects.begin(), Rule9Objects.end());

    errs() << "Counting total data pointers\n";

    auto objPtrPair = getTotalDataPointers(svfg);
    auto totalDataPointers = objPtrPair.first;
    auto totalDataObjects = objPtrPair.second;
    totalDataPointers.insert(CombinedSet.begin(), CombinedSet.end());

    errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
           << Rule1Set.size() << "(" << Rule1Result.PrioritizedPtrMap.size() << ") "
           << Rule2Set.size() << "(" << Rule2Result.PrioritizedPtrMap.size() << ") "
           << Rule3Set.size() << "(" << Rule3Result.PrioritizedPtrMap.size() << ") "
           << "-" << " "
           << Rule5Set.size() << "(" << Rule5Result.PrioritizedPtrMap.size() << ") "
           << Rule6Set.size() << "(" << Rule6Result.PrioritizedPtrMap.size() << ") "
           << Rule7Set.size() << "(" << Rule7Result.PrioritizedPtrMap.size() << ") "
           << "-" << " "
           << Rule9Set.size() << "(" << Rule9Result.PrioritizedPtrMap.size() << ") "
           << CombinedSet.size() << "(" << CombinedObjects.size() << ") " << "\n";


    return DPPAnalysis::Result("not implemented\n");
}

PreservedAnalyses DPPAnalysisPrinterPass::run(Module &M, AnalysisManager<Module> &AM) {
  OS << "Data Pointer Prioritization Analysis\n";

  auto Results = AM.getResult<DPPAnalysis>(M);
  Results.print(OS);

  return PreservedAnalyses::all();
}
