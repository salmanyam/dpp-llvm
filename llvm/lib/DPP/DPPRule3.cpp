//
// Created by salman on 7/4/21.
//

#include <chrono>

#include "llvm/DPP/SVFInitPass.h"
#include "llvm/DPP/DPPRule1.h"
#include "llvm/DPP/DPPRule3.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

#define DEBUG_TYPE "DPPRule3"

using namespace llvm;
using namespace llvm::DPP;
using namespace SVF;

[[maybe_unused]] const char DPPRule3G::RuleName[] = "DPPRule3G";
AnalysisKey DPPRule3G::Key;

const VFGNode* DPPRule3G::getVFGNodeFromValue(PAG *pag, SVFG *svfg, const Value *val) {
    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(val));
    const VFGNode* vNode = svfg->getDefSVFGNode(pNode);
    return vNode;
}

ValSet DPPRule3G::getPointersToObject(const Value *Val, SVFG *svfg) {
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

ValSet DPPRule3G::GetCompleteUsers(const Value *Val, SVFG *svfg) {
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

bool DPPRule3G::CheckInstInLoopPredecessor(Loop *L, const Instruction *I) {
    BasicBlock *BB = L->getLoopPredecessor();
    if (BB != nullptr) {
        for(auto b = BB->begin(), e = BB->end(); b != e; ++b){
            Instruction *Inst = &*b;
            //errs() << *Inst << "\n";

            auto Op = Inst->getOpcode();
            if (Op == Instruction::ICmp || Op == Instruction::FCmp) {
                if (Inst == I) {
                    //errs() << "First: " << *Inst << "Second: " << *I << "\n";
                    return true;
                }
            }
        }
    }
    return false;
}

bool DPPRule3G::CheckInstInLoopHeader(Loop *L, const Instruction *I) {
    BasicBlock *BB = L->getHeader();
    if (BB != nullptr) {
        for(auto b = BB->begin(), e = BB->end(); b != e; ++b){
            Instruction *Inst = &*b;
            //errs() << *Inst << "\n";

            auto Op = Inst->getOpcode();
            if (Op == Instruction::Load || Op == Instruction::Store || Op == Instruction::ICmp ||
            Op == Instruction::FCmp) {
                if (Inst == I) {
                    //errs() << "First: " << *Inst << "Second: " << *I << "\n";
                    return true;
                }
            }

        }
    }
    return false;
}

bool DPPRule3G::CheckInstInLoopLatch(Loop *L, const Instruction *I) {
    BasicBlock *BB = L->getLoopLatch();
    if (BB != nullptr) {
        for(auto b = BB->begin(), e = BB->end(); b != e; ++b){
            Instruction *Inst = &*b;
            //errs() << *Inst << "\n";

            auto Op = Inst->getOpcode();
            if (Op == Instruction::Load || Op == Instruction::Store || Op == Instruction::ICmp ||
                Op == Instruction::FCmp) {
                if (Inst == I) {
                    //errs() << "First: " << *Inst << "Second: " << *I << "\n";
                    return true;
                }
            }

        }
    }
    return false;
}

bool DPPRule3G::CheckUsageOfDPInstInLoop(const Instruction *I, LoopSet LS) {
    //bool result = false;
    for (auto L: LS) {
        /// skip to next loop if the loop does not contain the instruction
        if (! L->contains(I))
            continue;

        /// check the existence of the instruction in loop header and latch
        //errs() << "Check header\n";
        bool existInHeader = CheckInstInLoopHeader(L, I);
	
	if (existInHeader) 
	    return true;

        //errs() << "Check latch\n";
        bool existInLatch = CheckInstInLoopLatch(L, I);
	
	if (existInLatch)
	    return true;

        //errs() << "Check predecessor\n";
        bool existInPredecessor = CheckInstInLoopPredecessor(L, I);
	
	if (existInPredecessor)
	    return true;

        //errs() << "pred: " << existInPredecessor << " header: " << existInHeader << " latch: " << existInLatch << "\n";
        //result = existInHeader | existInLatch | existInPredecessor;

        //if (result)
          //  return result;
    }

    //return result;
    return false;
}

DPPRule3G::LoopSet DPPRule3G::TraverseLoops(LoopInfo &Loops) {
    FIFOWorkList<Loop *> worklist;
    LoopSet visited;

    /// Traverse along all uses of Val, insert the initial users list to the worklist
    for(LoopInfo::iterator it = Loops.begin(), e=Loops.end(); it != e; ++it){
        Loop *L = *it;
        worklist.push(L);
    }

    while (!worklist.empty()) {

        Loop *L = worklist.pop();
        //errs() << "POPPING: " << L->getLoopDepth() << "\n";
        visited.insert(L);

        vector<Loop *> SubLoops = L->getSubLoops();
        for(vector<Loop *>::iterator sl = SubLoops.begin(); sl != SubLoops.end(); sl++){
            Loop *LP = *sl;

            if (visited.find(LP) == visited.end())
                worklist.push(LP);
        }
    }

    return visited;
}

DPPRule3G::Result DPPRule3G::run(Module &M, AnalysisManager<Module> &AM) {
    Result Result {};

    using std::chrono::high_resolution_clock;
    using std::chrono::duration;
    duration<double, std::milli> runtime_ms;

    auto R = AM.getResult<SVFInitPass>(M);

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *CallGraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    LLVM_DEBUG(dbgs() << "Starting rule 3...\n");

    //auto DPValues = DPP::GetDataPointerInstructions(svfg, false);
    auto TaintedObjects = AM.getResult<DPPRule1G>(M);

    auto t1 = high_resolution_clock::now();

    /// store the users of a value to a map
    ValUserMap VUMap;
    for (auto Item: TaintedObjects.PrioritizedPtrMap) { // previously here was DPValues
        /// DP users list also include itself (DPInst) as a user
        auto DPUsers = GetCompleteUsers(Item.getFirst(), svfg); // previously was only Item
        VUMap.try_emplace(Item.getFirst(), DPUsers);
    }

    LLVM_DEBUG(dbgs() << "Checking the existence of data pointer in loops...\n");

    /// write some logs to file
    string dppLog = "#################### RULE 3 #########################\n";

    ValSet AlreadyCovered;

    /// getting the loop info from Loop Analysis pass for each function
    auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
    for (auto &F: M) {
        // skip the intrinsic or declaration functions
        if (F.isIntrinsic() || F.isDeclaration())
            continue;

        LLVM_DEBUG(dbgs() << F.getName() << "\n");
        LLVM_DEBUG(dbgs()  << "------------------------------\n");

        /// get the loop info
        LoopInfo &Loops = FAM.getResult<LoopAnalysis>(F);
        auto AllLoops = TraverseLoops(Loops);

        /// For each data pointer instruction, check its users existence in loops
        for (auto Item: VUMap) {
            auto Users = Item.getSecond();

            /// skip the items that have been already tested and they have some values which have been used in loops
            if (AlreadyCovered.find(Item.getFirst()) != AlreadyCovered.end())
                continue;

            for (auto User: Users) {
                //errs() << "User: " << *User << "\n";
                if (const auto *I = SVFUtil::dyn_cast<Instruction>(User)) {

                    /// only consider the load, store, and cmp instructions because we only check the existence
                    /// of these instructions in a loop
                    auto Op = I->getOpcode();
                    if (Op == Instruction::Load || Op == Instruction::Store ||
                        Op == Instruction::ICmp || Op == Instruction::FCmp)
                    {
                        bool existInLoop = CheckUsageOfDPInstInLoop(I, AllLoops);
                        if (existInLoop) {
                            /// some values have been used in loops
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
    }

    for (auto Item: AlreadyCovered) {
      Result.PrioritizedPtrMap.try_emplace(Item, 1);
    }

    auto t2 = high_resolution_clock::now();

    dppLog += "##################################################\n\n\n";
    if (DPP::isLogIndividualRule())
        DPP::writeDPPLogsToFile(dppLog);

    runtime_ms = t2 - t1;

    std::cout.precision(2);
    std::cout << "Rule3 done...time taken = " << std::fixed << runtime_ms.count()/1000 << "\n";
    
    return Result;
}


raw_ostream &DPPRule3GResult::print(raw_ostream &OS) const {
    OS << "Prioritized data objects by Rule 3:\n";
    for (auto Ptr : PrioritizedPtrMap) {
        auto *const I = Ptr.getFirst();
        auto score = Ptr.getSecond();
        OS << *I << ", score = " << score << "\n";
    }
    return OS;
}
