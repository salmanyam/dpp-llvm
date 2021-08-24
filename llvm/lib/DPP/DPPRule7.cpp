//
// Created by salman on 7/12/21.
//

#include "llvm/DPP/SVFInitPass.h"
#include "llvm/DPP/DPPRule7.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

#define DEBUG_TYPE "DPPRule7"

using namespace llvm;
using namespace llvm::DPP;
using namespace SVF;


[[maybe_unused]] const char DPPRule7G::RuleName[] = "DPPRule7G";
AnalysisKey DPPRule7G::Key;

const VFGNode* DPPRule7G::getVFGNodeFromValue(PAG *pag, SVFG *svfg, const Value *val) {
    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(val));
    const VFGNode* vNode = svfg->getDefSVFGNode(pNode);

    return vNode;
}

ValSet DPPRule7G::getPointersToObject(const Value *Val, SVFG *svfg) {
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

ValSet DPPRule7G::GetCompleteUsers(const Value *Val, SVFG *svfg) {
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



bool DPPRule7G::HasUnsafeCasting(const Instruction * I, Module &M) {
    if (isa<BitCastInst>(I)) {
        Type *DstTy = I->getType();

        auto bitCast = dyn_cast<BitCastInst>(I);

        Type *srcType = bitCast->getSrcTy();
        Type *dstType = bitCast->getDestTy();

        //errs() << *DstTy << " " << *srcType << " " << *dstType << "\n";

        if (!dstType->isPointerTy())
            return false;

        if (dstType->getContainedType(0)->isIntegerTy())
            return false;

        if (srcType->isPointerTy() && srcType->getContainedType(0)->isIntegerTy())
            return false;

        if (srcType == dstType)
            return false;

        //errs() << *I << "\n";

        //todo: need to check two things: 1) base and derive class or struct 2) size of two types
        /// 1) check the sizes
        //todo:%25 = bitcast i32 (...)* %21 to i32 (%struct.connection*, %struct.plugin_data_base*, ...)*, !dbg !14330
        //check if function pointer may be??
        auto srcContainedTypeSize = M.getDataLayout().getTypeAllocSizeInBits(srcType->getContainedType(0));
        auto dstContainedTypeSize = M.getDataLayout().getTypeAllocSizeInBits(dstType->getContainedType(0));

        if (srcContainedTypeSize != dstContainedTypeSize) {
            //errs() << *I << "\n";
            return true;
        }


        //todo: 2) how to check base and derived class or struct
    }

    return false;
}


DPPRule7G::Result DPPRule7G::run(Module &M, AnalysisManager<Module> &AM) {
    Result Result {};

    auto R = AM.getResult<SVFInitPass>(M);

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *CallGraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    LLVM_DEBUG(dbgs() << "Starting rule 7...\n");

    auto DPValues = GetDataPointerInstructions(svfg, false);

    /// store the users of a value to a map
    ValUserMap VUMap;
    for (auto DPVal: DPValues) {
        /// DPusers list also include DPInst as a user
        auto DPUsers = GetCompleteUsers(DPVal, svfg);
        VUMap.try_emplace(DPVal, DPUsers);
    }

    LLVM_DEBUG(dbgs() << "Checking bad casting of data pointers...\n");

    /// write some logs to file
    string dppLog = "#################### RULE 7 #########################\n";

    ValSet AlreadyCovered;

    /// For each data pointer instruction, check its users existence in loops
    for (auto Item: VUMap) {
        auto Users = Item.getSecond();
        //errs() << "value = " << *Item.getFirst() << "\n";
        /// skip the items that have been already tested and they have some values which have been used in loops
        if (AlreadyCovered.find(Item.getFirst()) != AlreadyCovered.end())
            continue;

        for (auto User: Users) {
            //errs() << "User: " << *User << "\n";
            if (const auto *I = SVFUtil::dyn_cast<Instruction>(User)) {

                /// get the bitcast instruction to check the source and destination type
                auto Op = I->getOpcode();
                if (Op == Instruction::BitCast) {
                    bool UnsafeCast = HasUnsafeCasting(I, M);

                    if (UnsafeCast) {
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

raw_ostream &DPPRule7GResult::print(raw_ostream &OS) const {
    OS << "Prioritized data objects by Rule 7:\n";
    for (auto Ptr : PrioritizedPtrMap) {
        auto *const I = Ptr.getFirst();
        auto score = Ptr.getSecond();
        OS << *I << ", score = " << score << "\n";
    }
    return OS;
}

