//
// Created by salman on 3/8/21.
//
#include <queue>
#include "set"

#include "llvm/DPP/SVFInitPass.h"
#include "llvm/DPP/DPPRule1.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

//DenseMap<const Value *, int32_t> Rule1Global(PAG *pag, PTACallGraph* callgraph, SVFG *svfg);

using namespace llvm;
using namespace llvm::DPP;
using namespace std;
using namespace SVF;

#define DEBUG_TYPE "DPPRule1"

AnalysisKey DPPRule1G::Key;
[[maybe_unused]] const char DPPRule1G::RuleName[] = "DPPRule1G";

void DPPRule1G::Rule1Init() {
    LLVM_DEBUG(dbgs() << "Initializing Rule 1\n");

    // clear any SVF nodes from the dataset
    InputSVFGNodes.clear();
    TaintedSVFNodes.clear();
    UnsafeSVFNodes.clear();

    // clear and obtain input functions
    InputFunctions.clear();

    auto IF = DPP::getInputLibFunctions();
    for(auto item: IF) {
        //todo: marking the return type as input dependent
        InputFunction Func = {StringRef(item->c_str()), 0, false};
        InputFunctions.try_emplace(Func.name, &Func);
    }
}


bool DPPRule1G::isInputReadingFunction(StringRef funcName) {
    if (InputFunctions.find(funcName) != InputFunctions.end())
        return true;
    return false;
}

unsigned int DPPRule1G::getInputArgStart(StringRef funcName) {
    return InputFunctions.lookup(funcName)->inputArgStart;
}

const VFGNode* DPPRule1G::getVFGNodeFromValue(PAG *pag, SVFG *svfg, const Value *val) {
    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(val));
    const VFGNode* vNode = svfg->getDefSVFGNode(pNode);
    return vNode;
}

DenseSet<SVF::PAGNode *> DPPRule1G::getPointedObjectsByPtr(const Value *Ptr, SVFG *svfg) {
    DenseSet<SVF::PAGNode *> pointsToObjects;
    // get the points-to set
    NodeID pNodeId = svfg->getPAG()->getValueNode(Ptr);
    const NodeBS& pts = svfg->getPTA()->getPts(pNodeId);
    for (unsigned int pt : pts)
    {
        if (!svfg->getPAG()->hasGNode(pt))
            continue;

        PAGNode* targetObj = svfg->getPAG()->getPAGNode(pt);
        if(targetObj->hasValue())
        {
            LLVM_DEBUG(dbgs() << *targetObj << "\n");
            pointsToObjects.insert(targetObj);
        }
    }
    return pointsToObjects;
}

/*!
 * This function traverse the SVF graph and collect taint list
 * by following nodes that are dependent on user input
 */
void DPPRule1G::updateTaintList(SVFG *svfg, const VFGNode* arg_vNode) {

    if (TaintedSVFNodes.find(arg_vNode->getId()) != TaintedSVFNodes.end())
        return;

    FIFOWorkList<const VFGNode*> worklist;
    //Set<const VFGNode*> visited;

    worklist.push(arg_vNode);
    //visited.insert(arg_vNode);

    /// Traverse along VFG
    while (!worklist.empty())
    {
        const VFGNode* vNode = worklist.pop();
        LLVM_DEBUG(dbgs() << "TAINTING: " << *vNode << "\n");
        //errs() << "TAINTING: " << *vNode << "\n";

        TaintedSVFNodes.insert(vNode->getId());

        // if an allocation site is found, then propagate the input dependency to all successor nodes
        if (vNode->getNodeKind() == VFGNode::VFGNodeK::Addr) {
            UnsafeSVFNodes.insert(vNode->getId());
        }

        for (auto it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd(); it != eit; ++it) {
            VFGEdge *edge = *it;
            VFGNode *dstNode = edge->getDstNode();

            //if (visited.find(dstNode) == visited.end()) {
            if (TaintedSVFNodes.find(dstNode->getId()) == TaintedSVFNodes.end()) {
                worklist.push(dstNode);
                //visited.insert(dstNode);
            }
        }

        if (vNode->getNodeKind() == VFGNode::VFGNodeK::Load) {
            if(const auto *LdVFG = SVFUtil::dyn_cast<LoadVFGNode>(vNode)) {
                if (LdVFG->getInst() != nullptr) {
                    if(const auto *LI = SVFUtil::dyn_cast<LoadInst>(LdVFG->getInst())) {
                        // we know the instruction is load, so get the first operand
                        auto Operand1 = LI->getOperand(0);

                        /// get objects pointed by the operand and taint the objects and their successors
                        auto objPointsToSet = getPointedObjectsByPtr(Operand1, svfg);
                        //errs() << "printing pointed objects...\n";
                        for (auto Item: objPointsToSet) {
                            const VFGNode *objVFGNode = getVFGNodeFromValue(svfg->getPAG(), svfg, Item->getValue());
                            if (TaintedSVFNodes.find(objVFGNode->getId()) == TaintedSVFNodes.end()) {
                                //errs() << *objVFGNode << "\n";
                                worklist.push(objVFGNode);
                                //visited.insert(objVFGNode);
                            }
                        }
                        //errs() << "end printing pointed objects...\n";
                    }
                }
            }
        }

        // if the VFG node is a store and if the first param is stored on the second param of a store instruction,
        // then include the second param as well.
        if (vNode->getNodeKind() == VFGNode::VFGNodeK::Store) {
            if(const auto *StVFG = SVFUtil::dyn_cast<StoreVFGNode>(vNode)) {
                if (StVFG->getInst() != nullptr) {
                    //errs() << *StVFG->getInst() << "\n";
                    if(const auto *SI = SVFUtil::dyn_cast<StoreInst>(StVFG->getInst())) {
                        // we know the instruction is store, so get the second operand
                        auto Operand2 = SI->getOperand(1);

                        /// get objects pointed by the operand and taint the objects and their successors
                        auto objPointsToSet = getPointedObjectsByPtr(Operand2, svfg);
                        //errs() << "printing pointed objects...\n";
                        for (auto Item: objPointsToSet) {
                            const VFGNode *objVFGNode = getVFGNodeFromValue(svfg->getPAG(), svfg, Item->getValue());
                            if (TaintedSVFNodes.find(objVFGNode->getId()) == TaintedSVFNodes.end()) {
                                //errs() << *objVFGNode << "\n";
                                worklist.push(objVFGNode);
                                //visited.insert(objVFGNode);
                            }
                        }

                        /// may not need it as this comes as a child node of the points-to object
                        // fetch the predecessor nodes and compare the operand2
                        for (auto it = vNode->InEdgeBegin(), eit = vNode->InEdgeEnd(); it != eit; ++it) {
                            VFGEdge *edge = *it;

                            // if storing to a variable indirectly, discard it for now
                            // todo: visit here to think about the indirect store
                            if (edge->isIndirectVFGEdge())
                                continue;

                            VFGNode *predNode = edge->getSrcNode();

                            // no need to consider the already visited one
                            //if (visited.find(predNode) == visited.end()) {
                            if (TaintedSVFNodes.find(predNode->getId()) == TaintedSVFNodes.end()) {
                                // Only considering the statement nodes
                                // todo: revisit here to consider other nodes such as memory region and params
                                if(auto *Smt = SVFUtil::dyn_cast<StmtVFGNode>(predNode)) {
                                    // Insert this node if the following is true, i.e.,
                                    // user dependent variable is being stored in operand2
                                    if (Smt->getInst() == Operand2) {
                                        worklist.push(Smt);
                                        //visited.insert(Smt);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

DPPRule1G::Result DPPRule1G::run(Module &M, AnalysisManager<Module> &AM) {
    Result Result{};

    auto R = AM.getResult<SVFInitPass>(M);

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *CallGraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    Rule1Init();

    LLVM_DEBUG(dbgs() << "Rule1 initialization done...\n");
    errs() << "Rule1 initialization done...\n";
    //svfg->dump("/home/salman/DPP/data/common.svfg", false);
    svfg->dump("/home/salman/DPP/data/rules-minimal/figure/figure2.svfg", false);


    /// construct the ICFG graph to get the compare instructions to approximate whether
    /// a variable or memory allocation is bounded or not
    ICFG *icfg = pag->getICFG();
    //icfg->dump("/home/salman/DPP/data/rules-minimal/figure/figure2.icfg", true);

    LLVM_DEBUG(dbgs() << "First node = " << *icfg->begin()->second << "\n");
    LLVM_DEBUG(dbgs() << "Total nodes = " << icfg->getTotalNodeNum() << "\n");

    /// get the entry block using global block node, the entry block is the parent of global block
    /// the entry block function is the main function
    auto gBN = icfg->getGlobalBlockNode();
    ICFGNode *entryBlock = nullptr;
    for (auto it = gBN->InEdgeBegin(), eit = gBN->InEdgeEnd(); it != eit; ++it) {
        ICFGEdge *edge = *it;
        entryBlock = edge->getSrcNode();
        break;
    }

    if (entryBlock && entryBlock->getFun()) {
        auto mainFunc = entryBlock->getFun()->getLLVMFun();
        /// tainting argument of main function
        if (mainFunc) {
            if (isInputReadingFunction(mainFunc->getName())) {
                LLVM_DEBUG(dbgs() << "Tainting main function = " << mainFunc->getName() << "...\n");
                for(auto Arg = mainFunc->arg_begin(); Arg != mainFunc->arg_end(); ++Arg) {
                    //errs() << *Arg << "\n";
                    if (auto *ArgVal = dyn_cast<Value>(Arg)) {
                        //errs() << "Arg value = " << *ArgVal << "\n";
                        const VFGNode *vfgNode = getVFGNodeFromValue(pag, svfg, ArgVal);
                        updateTaintList(svfg, vfgNode);
                    }
                }
            }
        }
    }

    LLVM_DEBUG(dbgs() << "Tainting parameters of other functions...\n");

    //NodeSet alreadyTaintedObjVFGNode;
    /// Filter input reading functions, and mark the nodes dependent on input reading functions
    for(const CallBlockNode *CS: pag->getCallSiteSet()) {
        /// get the names of the calle functions to filter input functions
        PTACallGraph::FunctionSet callees;
        CallGraph->getCallees(CS,callees);
        for(auto func : callees) {
            if (isInputReadingFunction(func->getName())) {
                LLVM_DEBUG(dbgs() << func->getName() << "\n");
                //errs() << "Function = " << func->getName() << "\n";

                unsigned int paramIndex = 0;
                for (auto P: CS->getActualParms()) {
                    LLVM_DEBUG(dbgs() << "Param " << paramIndex << ": " << *P << "\n");
                    //errs() << "Param " << paramIndex << ": " << *P << "\n";
                    paramIndex++;

                    /// skip dummy pag nodes
                    if (!P->hasValue())
                        continue;

                    /// skip constant type parameter
                    if(SVFUtil::isa<Constant>(P->getValue())) {
                        continue;
                    }

                    /// skip a constant getelemptr that is deduced from constants, e.g., getelemetptr str_0, 0, 0
                    if (DPP::isConstantGetElemInst(P->getValue())) {
                        continue;
                    }

                    LLVM_DEBUG(dbgs() << "Param SVFGNode ID = " << *svfg->getDefSVFGNode(P) << "\n");

                    /// update the taint list with all successors starting from the svf node of param P
                    updateTaintList(svfg, svfg->getDefSVFGNode(P));

                    /// get objects pointed by the operand and taint the objects and their successors
                    auto objPointsToSet = getPointedObjectsByPtr(P->getValue(), svfg);
                    for (auto Item: objPointsToSet) {
                        const VFGNode *objVFGNode = getVFGNodeFromValue(pag, svfg, Item->getValue());
                        if (TaintedSVFNodes.find(objVFGNode->getId()) == TaintedSVFNodes.end()) {
                            updateTaintList(svfg, objVFGNode);
                            //alreadyTaintedObjVFGNode.insert(objVFGNode->getId());
                            Result.TaintedSVFObjNodes.insert(objVFGNode->getId());
                        }
                    }
                }
                LLVM_DEBUG(dbgs() << "----------------------------------------------------\n");
            }
        }
    }

    /// Taint call instructions that have one or more tainted parameters
    /// propagate the taints through the return value of the call instructions
    for (auto SV = svfg->begin(); SV != svfg->end(); ++SV) {
        VFGNode *V = SV->second;
        if (auto *ARV = SVFUtil::dyn_cast<ActualRetVFGNode>(V)) {
            if (TaintedSVFNodes.find(ARV->getId()) != TaintedSVFNodes.end()) {
                /// just to make sure the taints get propagated
                updateTaintList(svfg, ARV);
                continue;
            }
            //errs() << *ARV->getCallSite()->getCallSite() << "\n";

            /// to tackle memory store operations
            if(const auto *CI = SVFUtil::dyn_cast<CallInst>(ARV->getCallSite()->getCallSite())) {
                //errs() << *CI << "\n";
                if (CI->getCalledFunction() != nullptr && CI->getCalledFunction()->hasName()) {
                    //errs() << CI->getCalledFunction()->getName() << "\n";
                    if (CI->getCalledFunction()->getName().contains("memcpy") ||
                        CI->getCalledFunction()->getName().contains("strcpy") ||
                        CI->getCalledFunction()->getName().contains("strncpy"))
                    {
                        //errs() << *CI << "\n";
                        //errs() << "ALL " << CI->getCalledFunction()->getName() << "\n";

                        auto Operand1 = CI->getOperand(0);
                        auto Operand2 = CI->getOperand(1);
                        /// get objects pointed by the operand and taint the objects and their successors
                        const VFGNode *objVFGNode2 = getVFGNodeFromValue(svfg->getPAG(), svfg, Operand2);
                        if (TaintedSVFNodes.find(objVFGNode2->getId()) != TaintedSVFNodes.end()) {
                            //errs() << *objVFGNode << "\n";
                            const VFGNode *objVFGNode1 = getVFGNodeFromValue(svfg->getPAG(), svfg, Operand1);
                            updateTaintList(svfg, objVFGNode1);
                            auto objPointsToSet = getPointedObjectsByPtr(Operand1, svfg);
                            for (auto Item: objPointsToSet) {
                                if(Item->hasValue())
                                    updateTaintList(svfg,
                                                    getVFGNodeFromValue(svfg->getPAG(), svfg, Item->getValue()));
                            }
                        }
                    }
                }
            }

            bool RetSVFGTainted = false;
            for (auto P: ARV->getCallSite()->getActualParms()) {
                /// skip dummy pag nodes
                if (!P->hasValue())
                    continue;

                /// skip constant type parameter
                if(SVFUtil::isa<Constant>(P->getValue())) {
                    continue;
                }

                /// skip a constant getelemptr that is deduced from constants, e.g., getelemetptr str_0, 0, 0
                if (DPP::isConstantGetElemInst(P->getValue())) {
                    continue;
                }
                auto P_SVF = svfg->getDefSVFGNode(P);
                if (TaintedSVFNodes.find(P_SVF->getId()) != TaintedSVFNodes.end()){
                    //errs() << "Param: " << *P_SVF << "\n";
                    RetSVFGTainted = true;
                    break;
                }
            }

            /// taint a ActualRetVFGNode (i.e., the return data) if any param is tainted
            if (RetSVFGTainted) {
                //errs() << *ARV << "\n";
                updateTaintList(svfg, ARV);
                Result.TaintedSVFObjNodes.insert(ARV->getId());
            }
        }
    }


    /// Taint store-type call instructions that have a tainted parameter and stored in another param, e.g., memcpy
    /// propagate the taints
    for (auto SV = svfg->begin(); SV != svfg->end(); ++SV) {
        VFGNode *vNode = SV->second;
        if (vNode->getNodeKind() == VFGNode::VFGNodeK::Store) {
            if(const auto *StVFG = SVFUtil::dyn_cast<StoreVFGNode>(vNode)) {
                if (StVFG->getInst() != nullptr) {
                    //errs() << *StVFG->getInst() << "\n";
                    if(const auto *CI = SVFUtil::dyn_cast<CallInst>(StVFG->getInst())) {
                        //if (TaintedSVFNodes.find(ARV->getId()) != TaintedSVFNodes.end())
                        //  continue;

                        if (CI->getCalledFunction()->getName().contains("memcpy") ||
                            CI->getCalledFunction()->getName().contains("strcpy") ||
                            CI->getCalledFunction()->getName().contains("strncpy"))
                        {
                            //errs() << *CI << "\n";
                            //errs() << "STORE " << CI->getCalledFunction()->getName() << "\n";

                            auto Operand1 = CI->getOperand(0);
                            auto Operand2 = CI->getOperand(1);
                            /// get objects pointed by the operand and taint the objects and their successors
                            const VFGNode *objVFGNode2 = getVFGNodeFromValue(svfg->getPAG(), svfg, Operand2);
                            if (TaintedSVFNodes.find(objVFGNode2->getId()) != TaintedSVFNodes.end()) {
                                //errs() << *objVFGNode << "\n";
                                const VFGNode *objVFGNode1 = getVFGNodeFromValue(svfg->getPAG(), svfg, Operand1);
                                updateTaintList(svfg, vNode);
                                updateTaintList(svfg, objVFGNode1);
                                auto objPointsToSet = getPointedObjectsByPtr(Operand1, svfg);
                                for (auto Item: objPointsToSet) {
                                    if(Item->hasValue())
                                        updateTaintList(svfg,
                                                        getVFGNodeFromValue(svfg->getPAG(), svfg, Item->getValue()));
                                }
                            }
                        }
                    }
                }
            }
        }
        if (vNode->getNodeKind() == VFGNode::VFGNodeK::Load) {
            if(const auto *StVFG = SVFUtil::dyn_cast<LoadVFGNode>(vNode)) {
                if (StVFG->getInst() != nullptr) {
                    //errs() << *StVFG->getInst() << "\n";
                    if(const auto *CI = SVFUtil::dyn_cast<CallInst>(StVFG->getInst())) {
                        //if (TaintedSVFNodes.find(ARV->getId()) != TaintedSVFNodes.end())
                        //  continue;

                        if (CI->getCalledFunction()->getName().contains("memcpy") ||
                            CI->getCalledFunction()->getName().contains("strcpy") ||
                            CI->getCalledFunction()->getName().contains("strncpy"))
                        {
                            //errs() << *CI << "\n";
                            //errs() << "Load " << CI->getCalledFunction()->getName() << "\n";

                            auto Operand1 = CI->getOperand(0);
                            auto Operand2 = CI->getOperand(1);
                            /// get objects pointed by the operand and taint the objects and their successors
                            const VFGNode *objVFGNode2 = getVFGNodeFromValue(svfg->getPAG(), svfg, Operand2);
                            if (TaintedSVFNodes.find(objVFGNode2->getId()) != TaintedSVFNodes.end()) {
                                //errs() << *objVFGNode << "\n";
                                const VFGNode *objVFGNode1 = getVFGNodeFromValue(svfg->getPAG(), svfg, Operand1);
                                updateTaintList(svfg, vNode);
                                updateTaintList(svfg, objVFGNode1);
                                auto objPointsToSet = getPointedObjectsByPtr(Operand1, svfg);
                                for (auto Item: objPointsToSet) {
                                    if(Item->hasValue())
                                        updateTaintList(svfg,
                                                        getVFGNodeFromValue(svfg->getPAG(), svfg, Item->getValue()));
                                }
                            }
                        }
                    }
                }
            }
        }
        if (vNode->getNodeKind() == VFGNode::VFGNodeK::Gep) {
            if(const auto *StVFG = SVFUtil::dyn_cast<GepVFGNode>(vNode)) {
                if (StVFG->getInst() != nullptr) {
                    //errs() << *StVFG->getInst() << "\n";
                    if(const auto *CI = SVFUtil::dyn_cast<CallInst>(StVFG->getInst())) {
                        //if (TaintedSVFNodes.find(ARV->getId()) != TaintedSVFNodes.end())
                        //  continue;

                        if (CI->getCalledFunction()->getName().contains("memcpy") ||
                            CI->getCalledFunction()->getName().contains("strcpy") ||
                            CI->getCalledFunction()->getName().contains("strncpy"))
                        {
                            //errs() << *CI << "\n";
                            //errs() << "GEP " << CI->getCalledFunction()->getName() << "\n";

                            auto Operand1 = CI->getOperand(0);
                            auto Operand2 = CI->getOperand(1);
                            /// get objects pointed by the operand and taint the objects and their successors
                            // auto objPointsToSet = getPointedObjectsByPtr(Operand2, svfg);
                            // errs() << objPointsToSet.size() << "\n";

                            const VFGNode *objVFGNode2 = getVFGNodeFromValue(svfg->getPAG(), svfg, Operand2);
                            if (TaintedSVFNodes.find(objVFGNode2->getId()) != TaintedSVFNodes.end()) {
                                //errs() << *objVFGNode << "\n";
                                const VFGNode *objVFGNode1 = getVFGNodeFromValue(svfg->getPAG(), svfg, Operand1);
                                updateTaintList(svfg, vNode);
                                updateTaintList(svfg, objVFGNode1);
                                auto objPointsToSet = getPointedObjectsByPtr(Operand1, svfg);
                                for (auto Item: objPointsToSet) {
                                    if(Item->hasValue())
                                        updateTaintList(svfg,
                                                        getVFGNodeFromValue(svfg->getPAG(), svfg, Item->getValue()));
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    LLVM_DEBUG(dbgs() << "Tainting address taken memory allocation nodes that have tainted dependency...\n");

    /// Taint the objects (pointed by function operand pointers) and their successors
    /// Taint each address taken memory allocation node if one or more operands are already tainted
    /// Taint get element pointer nodes that have one or more nodes tainted
    for (auto SV = svfg->begin(); SV != svfg->end(); ++SV) {
        VFGNode *V = SV->second;
        if (auto *AVF = SVFUtil::dyn_cast<AddrVFGNode>(V)) {
            /// skip already tainted object VFG nodes
            //if (alreadyTaintedObjVFGNode.find(AVF->getId()) != alreadyTaintedObjVFGNode.end())
            if (TaintedSVFNodes.find(AVF->getId()) != TaintedSVFNodes.end())
                continue;

            /// taint each address taken memory allocation node if one or more operands are already tainted
            /// rest of the nodes that does not satisfy AVF->getICFGNode()->getId() == 0 condition are instructions
            if (const auto *I = SVFUtil::dyn_cast<Instruction>(AVF->getInst())) {
                /// skip the alloca instruction because its operand does not have any associated SVF node
                if (I->getOpcode() == Instruction::Alloca)
                    continue;

                /// loop through all the operands and update the taint list if
                /// any operands of the instruction depend on user input
                for (auto Op = I->op_begin(); Op != I->op_end(); ++Op) {
                    const VFGNode* vNode = getVFGNodeFromValue(pag, svfg, Op->get());
                    /// if an operand is tainted, update the tainted list
                    if (TaintedSVFNodes.find(vNode->getId()) != TaintedSVFNodes.end()) {
                        /// get svf node for the instruction
                        auto vNodeInst = getVFGNodeFromValue(pag, svfg, I);
                        LLVM_DEBUG(dbgs() << "ADDR: " << *vNodeInst << "\n");

                        /// update the unsafe svf node
                        UnsafeSVFNodes.insert(vNodeInst->getId());

                        LLVM_DEBUG(dbgs() << "*********UPDATE TAINT LIST START: " << vNodeInst->getId() << "*********\n");
                        updateTaintList(svfg, vNodeInst);
                        LLVM_DEBUG(dbgs() << "*********UPDATE TAINT LIST END***********\n");

                        Result.TaintedSVFObjNodes.insert(vNodeInst->getId());

                        /// no need to look further because vNode is for the whole instruction,
                        /// other operand will also have the same instruction
                        break;
                    }
                }
            }
        }
        else if (V->getNodeKind() == VFGNode::VFGNodeK::Gep) {
            const auto *GepVFG = SVFUtil::dyn_cast<GepVFGNode>(V);
            if (const auto *I = SVFUtil::dyn_cast<Instruction>(GepVFG->getInst())) {
                bool foundTaintedOperand = false;
                /// loop through all the operands and update the taint list if
                /// any operands of the instruction depend on user input
                for (auto Op = I->op_begin(); Op != I->op_end(); ++Op) {
                    /// skip constant type operand
                    if(SVFUtil::isa<Constant>(Op->get())) {
                        continue;
                    }
                    /// skip a constant getelemptr that is deduced from constants, e.g., getelemetptr str_0, 0, 0
                    if (DPP::isConstantGetElemInst(Op->get())) {
                        continue;
                    }

                    const VFGNode* vNode = getVFGNodeFromValue(pag, svfg, Op->get());
                    if (TaintedSVFNodes.find(vNode->getId()) != TaintedSVFNodes.end()) {
                        foundTaintedOperand = true;
                        break;
                    }
                }
                /// if a tainted operand is found, find the pointed object of get element pointer instruction
                /// and insert the pointed object to unsafe object list.
                if (foundTaintedOperand) {
                    auto Operand1 = I->getOperand(0);
                    auto objPointsToSet = getPointedObjectsByPtr(Operand1, svfg);
                    for (auto Item: objPointsToSet) {
                        // lookup the svf node from the pag.value and insert the ID to unsafe node list
                        //UnsafeSVFNodes.insert(getVFGNodeFromValue(pag, svfg, Item->getValue())->getId());
                        const VFGNode* PtrvNode = getVFGNodeFromValue(pag, svfg, Item->getValue());
                        updateTaintList(svfg, PtrvNode);

                        Result.TaintedSVFObjNodes.insert(PtrvNode->getId());
                    }
                }
            }
        }
        /*else if (auto *ARV = SVFUtil::dyn_cast<ActualRetVFGNode>(V)) {
            if (TaintedSVFNodes.find(ARV->getId()) != TaintedSVFNodes.end())
                continue;

            bool RetSVFGTainted = false;
            for (auto P: ARV->getCallSite()->getActualParms()) {
                /// skip dummy pag nodes
                if (!P->hasValue())
                    continue;

                /// skip constant type parameter
                if(SVFUtil::isa<Constant>(P->getValue())) {
                    continue;
                }

                /// skip a constant getelemptr that is deduced from constants, e.g., getelemetptr str_0, 0, 0
                if (DPP::isConstantGetElemInst(P->getValue())) {
                    continue;
                }
                auto P_SVF = svfg->getDefSVFGNode(P);
                if (TaintedSVFNodes.find(P_SVF->getId()) != TaintedSVFNodes.end()){
                    //errs() << "Param: " << *P_SVF << "\n";
                    RetSVFGTainted = true;
                    break;
                }
            }

            /// taint a ActualRetVFGNode (i.e., the return data) if any param is tainted
            if (RetSVFGTainted) {
                //errs() << *ARV << "\n";
                updateTaintList(svfg, ARV);
                Result.TaintedSVFObjNodes.insert(ARV->getId());
            }
        }*/
    }

    LLVM_DEBUG(dbgs() << "Checking whether tainted nodes are bounded using heuristics...\n");

    /// write some logs to file
    //string dppLog = "#################### RULE 1 #########################\n";

    /// At this point, we have all the unsafe nodes. We loop through all the unsafe nodes,
    /// check if the nodes are bounded using compare instructions.
    for (auto ID: UnsafeSVFNodes) {
        const auto SVFNode = svfg->getSVFGNode(ID);
        //uint32_t nodeDepth = DepthMap.lookup(SVFNode->getICFGNode()->getId());
        //errs() << *SVFNode << ", Depth = " << DepthMap.lookup(SVFNode->getICFGNode()->getId()) << "\n";

        Type *PTy = NULL;
        if (auto *AVF = SVFUtil::dyn_cast<AddrVFGNode>(SVFNode)) {
            if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(AVF->getInst())) {
                if (I->getOpcode() == Instruction::Alloca) {
                    if (const AllocaInst *AI = SVFUtil::dyn_cast<AllocaInst>(I)) {
                        PTy = AI->getAllocatedType();
                    }
                } else if (I->getOpcode() == Instruction::Call) {
                    if (const CallInst *CI = SVFUtil::dyn_cast<CallInst>(I)) {
                        PTy = CI->getCalledFunction()->getReturnType();
                    }
                }

                /// if the type is data pointer
                if (PTy && DPP::isDataPointer(PTy)) {
                    /// storing the score so that we can pass the result
                    //R.try_emplace(I, 1);
                    Result.PrioritizedPtrMap.try_emplace(I, 1);
                }
            }
            else {
                /// add the global variable (may be) here
                if (AVF->getPAGSrcNode()->hasValue()) {
                    if (auto GlobalVar = llvm::dyn_cast<GlobalVariable>(AVF->getPAGSrcNode()->getValue())) {
                        if (DPP::isDataPointer(GlobalVar->getType()->getPointerElementType())) {
                            //R.try_emplace(GlobalVar, 1);
                            Result.PrioritizedPtrMap.try_emplace(GlobalVar, 1);
                        }
                    }
                }
            }
        }
    }

    //dppLog += "##################################################\n\n\n";
    //DPP::writeDPPLogsToFile(dppLog);

    return Result;
}

raw_ostream &DPPRule1GResult::print(raw_ostream &OS) const {
    OS << "Prioritized data objects by Rule 1:\n";
    for (auto Ptr : PrioritizedPtrMap) {
        auto *const I = Ptr.getFirst();
        auto score = Ptr.getSecond();
        OS << *I << ", score = " << score << "\n";
    }
    errs() << "Total tainted data objects = " << PrioritizedPtrMap.size() << "\n";
    return OS;
}
