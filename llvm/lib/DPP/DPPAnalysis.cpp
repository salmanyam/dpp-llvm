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

ValSet DPPAnalysis::getDataPointersToObject2(const Value *Val, SVFG *svfg) {
    ValSet totalDataPointers;
    //for (auto Item : Map) {
        //auto *const Val = Item.getFirst();
        //auto Pointers = getPointersToObject(Val, svfg);
        auto Pointers = GetCompleteUsers(Val, svfg);
        for (auto Ptr: Pointers) {
            if (const auto *I = dyn_cast<Instruction>(Ptr)) {
                if (I->getOpcode() == Instruction::Load || I->getOpcode() == Instruction::Store) {
                    totalDataPointers.insert(Ptr);
                }
            }
        }
    //}
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
    Result Result {};

    LLVM_DEBUG(dbgs() << "DPPGlobalAnalysis::run starting up...\n");
    //errs() << "DPPGlobalAnalysis::run starting up...\n";

    auto R = AM.getResult<SVFInitPass>(M);

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *CallGraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    LLVM_DEBUG(dbgs() << "Counting total data pointers\n");
    //errs() << "Counting total data pointers\n";

    ValSet AllDataObjects = DPP::GetDataPointerInstructions(svfg, true);

    auto objPtrPair = getTotalDataPointers(svfg);
    auto totalDataPointers = objPtrPair.first; // set of data pointers
    auto totalDataObjects = objPtrPair.second; // number of data objects

    auto ruleNum = DPP::getRuleNum();

    if (ruleNum.compare("rule1") == 0) {
        auto Rule1Result = AM.getResult<DPPRule1G>(M);
        auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);
        //auto Rule1Objects = getDataObjects(Rule1Result.PrioritizedPtrMap);

        /// store the load/store instructions in result
        Result.FilteredInstructions.insert(Rule1Set.begin(), Rule1Set.end());

        totalDataPointers.insert(Rule1Set.begin(), Rule1Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule1Set.size() << "(" << Rule1Result.PrioritizedPtrMap.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule2") == 0) {
        //auto Rule1Result = AM.getResult<DPPRule1G>(M);
        //auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);

        auto Rule2Result = AM.getResult<DPPRule2G>(M);
        /// filter rule 2
        //auto FilteredRule2Result = filterObjects(Rule2Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);
        auto Rule2Set = getDataPointersToObjects(Rule2Result.PrioritizedPtrMap, svfg);
        auto Rule2Objects = getDataObjects(Rule2Result.PrioritizedPtrMap);

        /// store the load/store instructions in result
        Result.FilteredInstructions.insert(Rule2Set.begin(), Rule2Set.end());

        totalDataPointers.insert(Rule2Set.begin(), Rule2Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule2Set.size() << "(" << Rule2Objects.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule3") == 0) {
        //auto Rule1Result = AM.getResult<DPPRule1G>(M);
        //auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);

        auto Rule3Result = AM.getResult<DPPRule3G>(M);
        /// filter rule 3
        //auto FilteredRule3Result = filterObjects(Rule3Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);
        auto Rule3Set = getDataPointersToObjects(Rule3Result.PrioritizedPtrMap, svfg);
        auto Rule3Objects = getDataObjects(Rule3Result.PrioritizedPtrMap);

        /// store the load/store instructions in result
        Result.FilteredInstructions.insert(Rule3Set.begin(), Rule3Set.end());

        totalDataPointers.insert(Rule3Set.begin(), Rule3Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule3Set.size() << "(" << Rule3Objects.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule4") == 0) {
        //auto Rule1Result = AM.getResult<DPPRule1G>(M);
        //auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);

        auto Rule4Result = AM.getResult<DPPRule4G>(M);
        /// filter rule 3
        //auto FilteredRule3Result = filterObjects(Rule3Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);
        auto Rule4Set = getDataPointersToObjects(Rule4Result.PrioritizedPtrMap, svfg);
        auto Rule4Objects = getDataObjects(Rule4Result.PrioritizedPtrMap);

        /// store the load/store instructions in result
        Result.FilteredInstructions.insert(Rule4Set.begin(), Rule4Set.end());

        totalDataPointers.insert(Rule4Set.begin(), Rule4Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule4Set.size() << "(" << Rule4Objects.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule5") == 0) {
        auto Rule5Result = AM.getResult<DPPRule5G>(M);
        auto Rule5Set = getDataPointersToObjects(Rule5Result.PrioritizedPtrMap, svfg);
        auto Rule5Objects = getDataObjects(Rule5Result.PrioritizedPtrMap);

        /// store the load/store instructions in result
        Result.FilteredInstructions.insert(Rule5Set.begin(), Rule5Set.end());

        totalDataPointers.insert(Rule5Set.begin(), Rule5Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule5Set.size() << "(" << Rule5Objects.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule6") == 0) {
        //auto Rule1Result = AM.getResult<DPPRule1G>(M);
        //auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);

        auto Rule6Result = AM.getResult<DPPRule6G>(M);
        /// filter rule 6
        //auto FilteredRule6Result = filterObjects(Rule6Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);
        auto Rule6Set = getDataPointersToObjects(Rule6Result.PrioritizedPtrMap, svfg);
        auto Rule6Objects = getDataObjects(Rule6Result.PrioritizedPtrMap);

        /// store the load/store instructions in result
        Result.FilteredInstructions.insert(Rule6Set.begin(), Rule6Set.end());

        totalDataPointers.insert(Rule6Set.begin(), Rule6Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule6Set.size() << "(" << Rule6Objects.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule7") == 0) {
        //auto Rule1Result = AM.getResult<DPPRule1G>(M);
        //auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);

        auto Rule7Result = AM.getResult<DPPRule7G>(M);
        /// filter rule 7
        //auto FilteredRule7Result = filterObjects(Rule7Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);
        auto Rule7Set = getDataPointersToObjects(Rule7Result.PrioritizedPtrMap, svfg);
        auto Rule7Objects = getDataObjects(Rule7Result.PrioritizedPtrMap);

        /// store the load/store instructions in result
        Result.FilteredInstructions.insert(Rule7Set.begin(), Rule7Set.end());

        totalDataPointers.insert(Rule7Set.begin(), Rule7Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule7Set.size() << "(" << Rule7Objects.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule8") == 0) {
        //auto Rule1Result = AM.getResult<DPPRule1G>(M);
        //auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);

        auto Rule8Result = AM.getResult<DPPRule8G>(M);
        /// filter rule 8 by considering buffers that are dependent on user, i.e., tainted
        //auto FilteredRule8Result = filterObjects(Rule8Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);

        auto Rule8Set = getDataPointersToObjects(Rule8Result.PrioritizedPtrMap, svfg);
        auto Rule8Objects = getDataObjects(Rule8Result.PrioritizedPtrMap);

        /// store the load/store instructions in result
        Result.FilteredInstructions.insert(Rule8Set.begin(), Rule8Set.end());

        totalDataPointers.insert(Rule8Set.begin(), Rule8Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule8Set.size() << "(" << Rule8Objects.size() << ") " << "\n";
    }
    else if (ruleNum.compare("rule9") == 0) {
        //auto Rule1Result = AM.getResult<DPPRule1G>(M);
        //auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);

        auto Rule9Result = AM.getResult<DPPRule9G>(M);
        /// filter rule 9
        //auto FilteredRule9Result = filterObjects(Rule9Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);
        auto Rule9Set = getDataPointersToObjects(Rule9Result.PrioritizedPtrMap, svfg);
        auto Rule9Objects = getDataObjects(Rule9Result.PrioritizedPtrMap);

        /// store the load/store instructions in result
        Result.FilteredInstructions.insert(Rule9Set.begin(), Rule9Set.end());

        totalDataPointers.insert(Rule9Set.begin(), Rule9Set.end());

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule9Set.size() << "(" << Rule9Objects.size() << ") " << "\n";
    }
    else {
        //errs() << "Before rule results..\n";
        auto Rule1Result = AM.getResult<DPPRule1G>(M);
        auto Rule2Result = AM.getResult<DPPRule2G>(M);
        auto Rule3Result = AM.getResult<DPPRule3G>(M);
        //auto Rule4Result = AM.getResult<DPPRule4G>(M);
        auto Rule5Result = AM.getResult<DPPRule5G>(M);
        auto Rule6Result = AM.getResult<DPPRule6G>(M);
        auto Rule7Result = AM.getResult<DPPRule7G>(M);
        auto Rule8Result = AM.getResult<DPPRule8G>(M);
        auto Rule9Result = AM.getResult<DPPRule9G>(M);

        //errs() << "After rule results..\n";

        /// filter rule 2
        //auto FilteredRule2Result = filterObjects(Rule2Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);

        /// filter rule 3
        //auto FilteredRule3Result = filterObjects(Rule3Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);

        /// filter rule 6
        //auto FilteredRule6Result = filterObjects(Rule6Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);

        /// filter rule 7
        //auto FilteredRule7Result = filterObjects(Rule7Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);

        /// filter rule 8 by considering buffers that are dependent on user, i.e., tainted
        //auto FilteredRule8Result = filterObjects(Rule8Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);
        //errs() << "Before size = " << Rule8Result.PrioritizedPtrMap.size() << " After = " << FilteredRule8Result.size() << "\n";

        /// filter rule 9
        //auto FilteredRule9Result = filterObjects(Rule9Result.PrioritizedPtrMap, Rule1Result.PrioritizedPtrMap);

        auto Rule1Objects = getDataObjects(Rule1Result.PrioritizedPtrMap);
        auto Rule2Objects = getDataObjects(Rule2Result.PrioritizedPtrMap);
        auto Rule3Objects = getDataObjects(Rule3Result.PrioritizedPtrMap);
        //auto Rule4Objects = getDataObjects(Rule4Result.PrioritizedPtrMap);
        auto Rule5Objects = getDataObjects(Rule5Result.PrioritizedPtrMap);
        auto Rule6Objects = getDataObjects(Rule6Result.PrioritizedPtrMap);
        auto Rule7Objects = getDataObjects(Rule7Result.PrioritizedPtrMap);
        auto Rule8Objects = getDataObjects(Rule8Result.PrioritizedPtrMap);
        auto Rule9Objects = getDataObjects(Rule9Result.PrioritizedPtrMap);

        auto Rule1Set = getDataPointersToObjects(Rule1Result.PrioritizedPtrMap, svfg);
        auto Rule2Set = getDataPointersToObjects(Rule2Result.PrioritizedPtrMap, svfg);
        auto Rule3Set = getDataPointersToObjects(Rule3Result.PrioritizedPtrMap, svfg);
        //auto Rule4Set = getDataPointersToObjects(Rule4Result.PrioritizedPtrMap, svfg);
        auto Rule5Set = getDataPointersToObjects(Rule5Result.PrioritizedPtrMap, svfg);
        auto Rule6Set = getDataPointersToObjects(Rule6Result.PrioritizedPtrMap, svfg);
        auto Rule7Set = getDataPointersToObjects(Rule7Result.PrioritizedPtrMap, svfg);
        auto Rule8Set = getDataPointersToObjects(Rule8Result.PrioritizedPtrMap, svfg);
        auto Rule9Set = getDataPointersToObjects(Rule9Result.PrioritizedPtrMap, svfg);



        /// compute total data pointers, i.e., load/store instructions
        ValSet CombinedSet;
        //CombinedSet.insert(Rule1Set.begin(), Rule1Set.end());
        CombinedSet.insert(Rule2Set.begin(), Rule2Set.end());
        CombinedSet.insert(Rule3Set.begin(), Rule3Set.end());
        //CombinedSet.insert(Rule4Set.begin(), Rule4Set.end());
        CombinedSet.insert(Rule5Set.begin(), Rule5Set.end());
        CombinedSet.insert(Rule6Set.begin(), Rule6Set.end());
        CombinedSet.insert(Rule7Set.begin(), Rule7Set.end());
        CombinedSet.insert(Rule8Set.begin(), Rule8Set.end());
        CombinedSet.insert(Rule9Set.begin(), Rule9Set.end());

        /// store the load/store instructions in result
        //Result.FilteredInstructions.insert(CombinedSet.begin(), CombinedSet.end());

        /// compute total objects
        ValSet CombinedObjects;
        //CombinedObjects.insert(Rule1Objects.begin(), Rule1Objects.end());
        CombinedObjects.insert(Rule2Objects.begin(), Rule2Objects.end());
        CombinedObjects.insert(Rule3Objects.begin(), Rule3Objects.end());
        //CombinedObjects.insert(Rule4Objects.begin(), Rule4Objects.end());
        CombinedObjects.insert(Rule5Objects.begin(), Rule5Objects.end());
        CombinedObjects.insert(Rule6Objects.begin(), Rule6Objects.end());
        CombinedObjects.insert(Rule7Objects.begin(), Rule7Objects.end());
        CombinedObjects.insert(Rule8Objects.begin(), Rule8Objects.end());
        CombinedObjects.insert(Rule9Objects.begin(), Rule9Objects.end());

        /// Rank data object based number of times various rules flag them
        DPPMap DObjRanks;
        for (auto DObj: CombinedObjects) {
            //DObjRanks.try_emplace(DObj, 10);
            //DObjRanks.try_emplace(DObj, 20);
            //errs() << "Look up " << DObjRanks.lookup(DObj) << "\n";
            if (Rule2Objects.contains(DObj)) {
                if (DObjRanks.find(DObj) != DObjRanks.end()) {
                    auto rank = DObjRanks.lookup(DObj);
                    DObjRanks.erase(DObj);
                    DObjRanks.try_emplace(DObj, rank+1);
                } else {
                    DObjRanks.try_emplace(DObj, 1);
                }
            }

            if (Rule3Objects.contains(DObj)) {
                if (DObjRanks.find(DObj) != DObjRanks.end()) {
                    auto rank = DObjRanks.lookup(DObj);
                    DObjRanks.erase(DObj);
                    DObjRanks.try_emplace(DObj, rank+1);
                } else {
                    DObjRanks.try_emplace(DObj, 1);
                }
            }

            if (Rule5Objects.contains(DObj)) {
                if (DObjRanks.find(DObj) != DObjRanks.end()) {
                    auto rank = DObjRanks.lookup(DObj);
                    DObjRanks.erase(DObj);
                    DObjRanks.try_emplace(DObj, rank+1);
                } else {
                    DObjRanks.try_emplace(DObj, 1);
                }
            }

            if (Rule6Objects.contains(DObj)) {
                if (DObjRanks.find(DObj) != DObjRanks.end()) {
                    auto rank = DObjRanks.lookup(DObj);
                    DObjRanks.erase(DObj);
                    DObjRanks.try_emplace(DObj, rank+1);
                } else {
                    DObjRanks.try_emplace(DObj, 1);
                }
            }

            if (Rule7Objects.contains(DObj)) {
                if (DObjRanks.find(DObj) != DObjRanks.end()) {
                    auto rank = DObjRanks.lookup(DObj);
                    DObjRanks.erase(DObj);
                    DObjRanks.try_emplace(DObj, rank+1);
                } else {
                    DObjRanks.try_emplace(DObj, 1);
                }
            }

            if (Rule8Objects.contains(DObj)) {
                if (DObjRanks.find(DObj) != DObjRanks.end()) {
                    auto rank = DObjRanks.lookup(DObj);
                    DObjRanks.erase(DObj);
                    DObjRanks.try_emplace(DObj, rank+1);
                } else {
                    DObjRanks.try_emplace(DObj, 1);
                }
            }

            if (Rule9Objects.contains(DObj)) {
                if (DObjRanks.find(DObj) != DObjRanks.end()) {
                    auto rank = DObjRanks.lookup(DObj);
                    DObjRanks.erase(DObj);
                    DObjRanks.try_emplace(DObj, rank+1);
                } else {
                    DObjRanks.try_emplace(DObj, 1);
                }
            }
        }

        //std::vector<std::pair<const Value *, int>> DObjItems;
        std::vector<ObjectInfo> DObjItems;
        for (auto Item: DObjRanks) {
            auto Pointers = GetCompleteUsers(Item.getFirst(), svfg);
            auto LoadStorePtrs = getDataPointersToObject2(Item.getFirst(), svfg);
            ObjectInfo Obj = {Item.getFirst(), Item.getSecond(), LoadStorePtrs.size()};
            //DObjItems.push_back(std::make_pair(Item.getFirst(),
                                        //       Item.getSecond() * (0.5*Pointers.size() + 0.5*LoadStorePtrs.size())));
            DObjItems.push_back(Obj);
        }

        //auto cmp = [](std::pair<const Value *, int> const & a, std::pair<const Value *, int> const & b) {
          //  return a.second != b.second?  a.second > b.second : a.first > b.first;
        //};

        auto cmp = [](ObjectInfo const & a, ObjectInfo const & b) {
            return a.NumRulesFlagObj != b.NumRulesFlagObj?  a.NumRulesFlagObj > b.NumRulesFlagObj : a.NumPointers > b.NumPointers;
        };

        std::sort(DObjItems.begin(), DObjItems.end(), cmp);

        auto topk = int(round(DObjItems.size() * DPP::getNumTopKDObjs() / 100.0)); //top-k percent of data objects

        if (topk < 1)
            topk = 1;

        /// write some logs to file
        string dppLog = "#################### SUMMARY: " + to_string(topk) +" data objects #########################\n";

        errs() << "Top k = " << topk << "\n";

        int count = 0;
        for(auto Item: DObjItems) {
            if (count >= topk) break;
            count++;

	    auto LoadStorePtrs = getDataPointersToObject2(Item.Obj, svfg);
            Result.FilteredInstructions.insert(LoadStorePtrs.begin(), LoadStorePtrs.end());
            
	    auto SVFNode = getVFGNodeFromValue(pag, svfg, Item.Obj);
            errs() << SVFNode->toString() << "\t" << Item.NumRulesFlagObj << "\t" << Item.NumPointers << "\n";
            dppLog += SVFNode->toString() + "\t" +
                    to_string(Item.NumRulesFlagObj) + "\t" +
                    to_string(Item.NumPointers) + "\n";
            dppLog += "--------------------------------------------------------------\n";
        }

/*
        int count = 0;
        int lastItemNumRulesFlagObj = 0;
        int lastItemNumPointers = 0;
        for(auto Item: DObjItems) {
            if (count >= topk) break;
            count++;
            //errs() << *Item.first << "============>" << Item.second << "\n";
            lastItemNumRulesFlagObj = Item.NumRulesFlagObj;
            lastItemNumPointers = Item.NumPointers;
        }
        int actual_objects = 0;
        for(auto Item: DObjItems) {
            if (lastItemNumRulesFlagObj > 0 && Item.NumRulesFlagObj >= lastItemNumRulesFlagObj &&
            Item.NumPointers >= lastItemNumPointers) {
                actual_objects++;
                auto SVFNode = getVFGNodeFromValue(pag, svfg, Item.Obj);
                errs() << SVFNode->toString() << "\t" << Item.NumRulesFlagObj << "\t" << Item.NumPointers << "\n";
                dppLog += SVFNode->toString() + "\t" +
                        to_string(Item.NumRulesFlagObj) + "\t" +
                        to_string(Item.NumPointers) + "\n";
                dppLog += "--------------------------------------------------------------\n";
            }
        }*/


        /// Also store the objects
        /// it's done inside dobj items print loop
        //Result.FilteredInstructions.insert(CombinedObjects.begin(), CombinedObjects.end());

        /// in case, tht total pointer calculation missed some pointers
        totalDataPointers.insert(CombinedSet.begin(), CombinedSet.end());

        //errs() << "\nTOP-K = " + to_string(topk) + ", Actual = " + to_string(actual_objects) + "\n";

        errs() << totalDataPointers.size() << "(" << totalDataObjects << ") "
        << Rule1Set.size() << "(" << Rule1Objects.size() << ") "
        << Rule2Set.size() << "(" << Rule2Objects.size() << ") "
        << Rule3Set.size() << "(" << Rule3Objects.size() << ") "
        //        << Rule4Set.size() << "(" << Rule4Objects.size() << ") "
        << " - "
        << Rule5Set.size() << "(" << Rule5Objects.size() << ") "
        << Rule6Set.size() << "(" << Rule6Objects.size() << ") "
        << Rule7Set.size() << "(" << Rule7Objects.size() << ") "
        << Rule8Set.size() << "(" << Rule8Objects.size() << ") "
        << Rule9Set.size() << "(" << Rule9Objects.size() << ") "
        << CombinedSet.size() << "(" << CombinedObjects.size() << ") " << "\n";

        dppLog += "\nTOP-K = " + to_string(topk) + ", Actual = " + to_string(/*actual_objects*/0) + "\n"
                + to_string(totalDataPointers.size()) + "(" + to_string(totalDataObjects) + ") "
                + to_string(Rule1Set.size()) + "(" + to_string(Rule1Objects.size()) + ") "
                + to_string(Rule2Set.size()) + "(" + to_string(Rule2Objects.size()) + ") "
                + to_string(Rule3Set.size()) + "(" + to_string(Rule3Objects.size()) + ") "
                //        << Rule4Set.size() << "(" << Rule4Objects.size() << ") "
                + " - "
                + to_string(Rule5Set.size()) + "(" + to_string(Rule5Objects.size()) + ") "
                + to_string(Rule6Set.size()) + "(" + to_string(Rule6Objects.size()) + ") "
                + to_string(Rule7Set.size()) + "(" + to_string(Rule7Objects.size()) + ") "
                + to_string(Rule8Set.size()) + "(" + to_string(Rule8Objects.size()) + ") "
                + to_string(Rule9Set.size()) + "(" + to_string(Rule9Objects.size()) + ") "
                + to_string(CombinedSet.size()) + "(" + to_string(CombinedObjects.size()) + ") " + "\n";


        /*dppLog += "====================HERE STARTS ALL THE OBJECTS=====================\n";
        for(auto Item: AllDataObjects) {
            ///skip if prioritized objects have this item
            if (CombinedObjects.find(Item) != CombinedObjects.end())
                continue;

            auto SVFNode = getVFGNodeFromValue(pag, svfg, Item);
            dppLog += SVFNode->toString() + "\n";
            dppLog += "--------------------------------------------------------------\n";
        }*/

        DPP::writeDPPLogsToFile(dppLog);
    }

    return Result;
}

PreservedAnalyses DPPAnalysisPrinterPass::run(Module &M, AnalysisManager<Module> &AM) {
  OS << "Data Pointer Prioritization Analysis\n";

  auto Results = AM.getResult<DPPAnalysis>(M);
  Results.print(OS);

  return PreservedAnalyses::all();
}

raw_ostream &DPPAnalysisResult::print(raw_ostream &OS) const {
    OS << "DPP Analysis:\n";

    return OS;
}
