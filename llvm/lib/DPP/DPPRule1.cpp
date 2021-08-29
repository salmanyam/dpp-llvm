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


DenseMap<const Value *, int32_t> Rule1Global(PAG *pag, PTACallGraph* callgraph, SVFG *svfg);

using namespace llvm;
using namespace llvm::DPP;
using namespace std;
using namespace SVF;

#define DEBUG_TYPE "DPPRule1"

// to store input dependent library functions
InputFunctionMap InputFunctions;

// for tracking the SVFG nodes that are dependent on input
NodeSet InputSVFGNodes;
NodeSet TaintedSVFNodes;
NodeSet UnsafeSVFNodes;


void Rule1Init() {
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


bool isInputReadingFunction(StringRef funcName) {
    if (InputFunctions.find(funcName) != InputFunctions.end())
        return true;
    return false;
}

unsigned int getInputArgStart(StringRef funcName) {
    return InputFunctions.lookup(funcName)->inputArgStart;
}

const VFGNode* getVFGNodeFromValue(PAG *pag, SVFG *svfg, const Value *val) {
    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(val));
    const VFGNode* vNode = svfg->getDefSVFGNode(pNode);
    return vNode;
}

/*!
 * This function traverse the SVF graph and collect taint list
 * by following nodes that are dependent on user input
 */
void updateTaintList(SVFG *svfg, const VFGNode* arg_vNode) {
    FIFOWorkList<const VFGNode*> worklist;
    Set<const VFGNode*> visited;

    worklist.push(arg_vNode);
    visited.insert(arg_vNode);

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

            if (visited.find(dstNode) == visited.end()) {
                worklist.push(dstNode);
                visited.insert(dstNode);
            }
        }

        // if the VFG node is a store and if the first param is stored on the second param of a store instruction,
        // then include the second param as well.
        if (vNode->getNodeKind() == VFGNode::VFGNodeK::Store) {
            if(const auto *StVFG = SVFUtil::dyn_cast<StoreVFGNode>(vNode)) {
                if (StVFG->getInst() != nullptr) {
                    if(const auto *SI = SVFUtil::dyn_cast<StoreInst>(StVFG->getInst())) {
                        // we know the instruction is store, so get the second operand
                        auto Operand2 = SI->getOperand(1);

                        // fetch the predecessor nodes and compare the operand2
                        for (auto it = vNode->InEdgeBegin(), eit = vNode->InEdgeEnd(); it != eit; ++it) {
                            VFGEdge *edge = *it;

                            // if storing to a variable indirectly, discard it for now
                            // todo: visit here to think about the indirect store
                            if (edge->isIndirectVFGEdge())
                                continue;

                            VFGNode *predNode = edge->getSrcNode();

                            // no need to consider the already visited one
                            if (visited.find(predNode) == visited.end()) {
                                // Only considering the statement nodes
                                // todo: revisit here to consider other nodes such as memory region and params
                                if(auto *Smt = SVFUtil::dyn_cast<StmtVFGNode>(predNode)) {
                                    // Insert this node if the following is true, i.e.,
                                    // user dependent variable is being stored in operand2
                                    if (Smt->getInst() == Operand2) {
                                        worklist.push(Smt);
                                        visited.insert(Smt);
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

auto getReachableNodes(const VFGNode* vNode, PAG *pag, SVFG *svfg) {
    NodeSet vNodeIDs;

    FIFOWorkList<const VFGNode*> worklist;
    Set<const VFGNode*> visited;
    worklist.push(vNode);

    /// also get the operands and add the operand svf nodes
    const VFGNode* paramNode = NULL;
    if (auto *AVF = SVFUtil::dyn_cast<AddrVFGNode>(vNode)) {
        if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(AVF->getInst())) {
            ///skip alloca because the param of alloca instruction does not have VFG node
            if (I->getOpcode() != Instruction::Alloca) {
                for (auto Op = I->op_begin(); Op != I->op_end(); ++Op) {
                    //errs() << *Op->get() << "\n";
                    /// skip constant type parameter
                    if(SVFUtil::isa<Constant>(Op->get()))
                        continue;

                    paramNode = getVFGNodeFromValue(pag, svfg, Op->get());
                    //errs() << "REACHABLE NODE: " << *paramNode << "\n";
                    worklist.push(paramNode);
                }
            }
        }
    }

    /// Traverse along VFG
    while (!worklist.empty())
    {
        const VFGNode* pNode = worklist.pop();
        //errs() << "REACHABLE NODE: " << *pNode << "\n";
        vNodeIDs.insert(pNode->getId());

        for (VFGNode::const_iterator it = pNode->InEdgeBegin(), eit =
                pNode->InEdgeEnd(); it != eit; ++it) {
            VFGEdge *edge = *it;
            VFGNode *srcNode = edge->getSrcNode();

            if (visited.find(srcNode) == visited.end()) {
                visited.insert(srcNode);
                worklist.push(srcNode);
            }
        }
    }

    return vNodeIDs;
}
/**
 * Traverse along with the Inter-procedural CFG to obtain a list of compare instructions
 */
auto getCmpInstructions(const ICFGNode *icfgNode) {
    Set<const ICFGNode *> cmpNodes;
    FIFOWorkList<const ICFGNode*> worklist;
    Set<const ICFGNode*> visited;

    auto numBackwardCmps = getNumBackwardCmps();

    worklist.push(icfgNode);
    visited.insert(icfgNode);

    while (!worklist.empty()) {
        const ICFGNode *node = worklist.pop();

        // we are interested in intra block nodes only, other blocks are function entry, exit, global, etc.
        if (node->getNodeKind() == ICFGNode::IntraBlock) {
            if (auto *IBN = SVFUtil::dyn_cast<IntraBlockNode>(node)) {
                if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(IBN->getInst())) {
                    /// If we found a cmp instruction, insert it into our list
                    if (I->getOpcode() == Instruction::ICmp || I->getOpcode() == Instruction::FCmp) {
                        //errs() << *I << "\n";
                        if (cmpNodes.size() >= numBackwardCmps)
                            continue;

                        cmpNodes.insert(node);
                    }
                }
            }
        }

        /// for all incoming edges, get the source nodes and insert them into the worklist
        for (ICFGNode::const_iterator it = node->InEdgeBegin(), eit = node->InEdgeEnd(); it != eit; ++it) {
            ICFGEdge *edge = *it;
            ICFGNode *srcNode = edge->getSrcNode();

            if (visited.find(srcNode) == visited.end()) {
                visited.insert(srcNode);
                worklist.push(srcNode);
            }
        }
    }

    return cmpNodes;
}

auto getNumCmpsInPath(vector<const ICFGNode *> P) {
    int32_t count = 0;
    for (auto node: P) {
        if (node->getNodeKind() == ICFGNode::IntraBlock) {
            if (auto *IBN = SVFUtil::dyn_cast<IntraBlockNode>(node)) {
                if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(IBN->getInst())) {
                    if (I->getOpcode() == Instruction::ICmp || I->getOpcode() == Instruction::FCmp) {
                        count++;
                    }
                }
            }
        }
    }
    return count;
}

auto getCmpPaths(const ICFGNode *icfgNode) {
    vector<vector<const ICFGNode *>> toReturn;
    bool haveNewNode;
    auto numBackwardCmpPaths = getNumBackwardCmpPaths();
    auto numNodeInPathToConsider = getNumNodesInPath();

    queue<vector<const ICFGNode *>> worklist;
    vector<const ICFGNode *> path;
    path.push_back(icfgNode);
    worklist.push(path);

    /// Traverse along VFG
    while (!worklist.empty()) {
        auto curPath = worklist.front();
        worklist.pop();

        toReturn.push_back(curPath);

        if (curPath.size() > numNodeInPathToConsider)
            continue;

        auto node = curPath[curPath.size()-1];

        if (node->getNodeKind() == ICFGNode::IntraBlock) {
            if (auto *IBN = SVFUtil::dyn_cast<IntraBlockNode>(node)) {
                if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(IBN->getInst())) {
                    /// If we found a cmp instruction, insert it into our list
                    if (I->getOpcode() == Instruction::ICmp || I->getOpcode() == Instruction::FCmp) {
                        //errs() << *I << "\n";
                        if (getNumCmpsInPath(curPath) >= numBackwardCmpPaths)
                            continue;
                    }

                    /// determine if constant value is being stored, if so then skip the successor of this node
                    /*if (I->getOpcode() == Instruction::Store) {
                        if (const StoreInst *SI = SVFUtil::dyn_cast<StoreInst>(I)) {
                            if(SVFUtil::isa<Constant>(SI->getOperand(0))) {
                                //errs() << "Constant store found" << "\n";
                                continue;
                            }
                        }
                    }*/
                }
            }
        }

        /// check if all arguments of a call instruction is constant, if so skip the successors of this node
        /*if (node->getNodeKind() == ICFGNode::FunCallBlock) {
            if (auto *CBN = SVFUtil::dyn_cast<CallBlockNode>(node)) {
                //errs() << *CBN << "\n";
                //errs() << "-----------------------\n";

                /// if no parameters
                if (CBN->getActualParms().size() == 0)
                    continue;

                /// if all are constants
                isConstant = true;
                for (auto P: CBN->getActualParms()) {
                    //errs() << "Param = " << *P->getValue() << "\n";
                    if (!SVFUtil::isa<Constant>(P->getValue())) {
                        isConstant = false;
                        break;
                    }
                }
                /// skip if all params are constant
                if (isConstant) {
                    //errs() << "Constant params found\n";
                    continue;
                }
                //errs() << "Non-constant params found\n";
                //errs() << "-----------------------\n";
            }
        }*/

        /// for all incoming edges, get the source nodes and look for them
        haveNewNode = false;
        for (auto it = node->InEdgeBegin(), eit = node->InEdgeEnd(); it != eit; ++it) {
            ICFGEdge *edge = *it;
            ICFGNode *srcNode = edge->getSrcNode();

            // discard the link between ret block node to call block node and ret block node to func exist block node
            /*if (node->getNodeKind() == ICFGNode::ICFGNodeK::FunRetBlock) {
                if (srcNode->getNodeKind() == ICFGNode::ICFGNodeK::FunCallBlock ) {
                    if (auto *CBN = SVFUtil::dyn_cast<CallBlockNode>(srcNode)) {
                        if (auto *CI = SVFUtil::dyn_cast<CallInst>(CBN->getCallSite())) {
                            //errs() << "skip if not declaration: " << CI->getCalledFunction()->getName() << " IsDec = "
                             //      << CI->getCalledFunction()->isDeclaration() << "\n";
                            if (!CI->getCalledFunction()->isDeclaration())
                                continue;
                        }
                    }
                }
            }*/

            if (find(curPath.begin(), curPath.end(), srcNode) == curPath.end()) {
                vector<const ICFGNode *> newPath(curPath);
                newPath.push_back(srcNode);
                worklist.push(newPath);

                haveNewNode = true;
            }
        }

        /// discard the last entry from the to return list because we have more nodes to explore
        if (haveNewNode) {
            toReturn.pop_back();
        }

    }

    return toReturn;
}

auto checkCommonAncestor(list<const VFGNode *> vNodes, NodeSet toCompare) {
    FIFOWorkList<const VFGNode*> worklist;
    Set<const VFGNode*> visited;

    for (auto item: vNodes) {
        worklist.push(item);
    }

    /// Traverse along VFG
    while (!worklist.empty())
    {
        const VFGNode* pNode = worklist.pop();
        if (toCompare.find(pNode->getId()) != toCompare.end())
            return true;

        for (VFGNode::const_iterator it = pNode->InEdgeBegin(), eit =
                pNode->InEdgeEnd(); it != eit; ++it) {
            VFGEdge *edge = *it;
            VFGNode *srcNode = edge->getSrcNode();

            if (visited.find(srcNode) == visited.end()) {
                visited.insert(srcNode);
                worklist.push(srcNode);
            }
        }
    }

    return false;
}

auto assignDepth(const ICFGNode *firstNode, u64_t depth) {
    DenseMap<NodeID, u64_t> NodeDepths;

    /// update the depth of each node
    NodeDepths.try_emplace(firstNode->getId(), depth);

    FIFOWorkList<const ICFGNode*> worklist;
    Set<const ICFGNode*> visited;
    visited.insert(firstNode);
    worklist.push(firstNode);

    /// Traverse along ICFG
    while (!worklist.empty()) {
        const ICFGNode *node = worklist.pop();
        depth = NodeDepths.lookup(node->getId());
        //errs() << "DEPTH TRAVERSE: " << *node << ", Depth=" << depth << "\n";

        for (ICFGNode::const_iterator it = node->OutEdgeBegin(), eit =
                node->OutEdgeEnd(); it != eit; ++it) {
            ICFGEdge *edge = *it;
            ICFGNode *dstNode = edge->getDstNode();

            if (visited.find(dstNode) == visited.end()) {
                visited.insert(dstNode);
                worklist.push(dstNode);

                /// update the depth of each node
                NodeDepths.try_emplace(dstNode->getId(), depth+1);
            }
        }
    }

    LLVM_DEBUG(dbgs() << "Total traversed = " << visited.size() << "\n");

    return NodeDepths;
}

pair<int64_t, int64_t> approximateBound(Set<const ICFGNode*> cmpNodes) {
    int64_t approxUpperBound = LONG_MIN;
    int64_t approxLowerBound = LONG_MAX;

    for (auto node: cmpNodes) {
        if (node->getNodeKind() == ICFGNode::IntraBlock) {
            if (auto *IBN = SVFUtil::dyn_cast<IntraBlockNode>(node)) {
                if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(IBN->getInst())) {
                    /// to double check if it's a cmp instruction
                    if (I->getOpcode() == Instruction::ICmp || I->getOpcode() == Instruction::FCmp) {
                        //errs() << *I << "\n";
                        if (const CmpInst *CI = SVFUtil::dyn_cast<CmpInst>(I)) {
                            //errs() << "Predicate = " << CI->getPredicate() << "\n";
                            if (SVFUtil::isa<Constant>(CI->getOperand(1))) {
                                //errs() << "Constant 2nd operand found" << "\n";
                                if (ConstantInt *CInt = SVFUtil::dyn_cast<ConstantInt>(CI->getOperand(1))) {
                                    //errs() << "Constant = " << CInt->getValue().getSExtValue() << "\n";
                                    if (CInt->getValue().getSExtValue() < approxLowerBound)
                                        approxLowerBound = CInt->getValue().getSExtValue();
                                    if (CInt->getValue().getSExtValue() > approxUpperBound)
                                        approxUpperBound = CInt->getValue().getSExtValue();
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    //errs() << "lower bound = " << approxLowerBound << ", upper bound = " << approxUpperBound << "\n";

    return make_pair(approxLowerBound, approxUpperBound);
}

auto computePointerScore(int totalPaths, int relevantPaths, BoundApproxType BT) {
    int32_t score = 0;
    int32_t scale = 5;
    /// compute score based on the number of paths lead to the pointer node checked vs non-checked

    if (totalPaths > 0) {
        score += scale * ((totalPaths - relevantPaths) / (1.0 * totalPaths)) * NO_CMP;
        score += scale * (relevantPaths / (1.0 * totalPaths)) * YES_CMP;
    }

    /// score based on bound approximation
    if (BT == BoundApproxType::VARIABLE)
        score += HAVE_VARIABLE_BOUND;
    else
        score += HAVE_CONSTANT_BOUND;

    /// todo: score based safe vs unsafe uses

    return score;
}

bool IsDataPointerTmp(const Type *const T) {
    if (T && T->isPointerTy() && !T->isFunctionTy())
        return true;

    if (T && T->isArrayTy() && !T->isFunctionTy())
        return true;

    return false;
}

DenseSet<SVF::PAGNode *> getPointedObjectsByPtr(const Value *Ptr, SVFG *svfg) {
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

DenseMap<const Value *, int32_t> Rule1Global(PAG *pag, PTACallGraph* callgraph, SVFG *svfg) {
    /// to return the resultant prioritized dpp along with their scores
    DenseMap<const Value *, int32_t> R;

    Rule1Init();

    LLVM_DEBUG(dbgs() << "Rule1 initialization done...\n");

    /// construct the ICFG graph to get the compare instructions to approximate whether
    /// a variable or memory allocation is bounded or not
    ICFG *icfg = pag->getICFG();

    LLVM_DEBUG(dbgs() << "First node = " << *icfg->begin()->second << "\n");
    LLVM_DEBUG(dbgs() << "Total nodes = " << icfg->getTotalNodeNum() << "\n");


    /// get the entry block using global block node, the entry block is the parent of global block
    auto gBN = icfg->getGlobalBlockNode();
    ICFGNode *entryBlock = nullptr;
    for (auto it = gBN->InEdgeBegin(), eit = gBN->InEdgeEnd(); it != eit; ++it) {
        ICFGEdge *edge = *it;
        entryBlock = edge->getSrcNode();
        break;
    }
    assert(entryBlock != nullptr && "Entry block node in ICFG is NULL, can't assign depth!");

    /// insert main function here
    PTACallGraph::FunctionSet AllFunctions;
    AllFunctions.insert(entryBlock->getFun());

    auto mainFunc = entryBlock->getFun()->getLLVMFun();
    /// tainting argument of main function
    if (mainFunc) {
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

    LLVM_DEBUG(dbgs() << "Tainting parameters of other functions...\n");

    NodeSet alreadyTaintedObjVFGNode;
    /// Filter input reading functions, and mark the nodes dependent on input reading functions
    for(const CallBlockNode *CS: pag->getCallSiteSet()) {
        /// get the names of the calle functions to filter input functions
        PTACallGraph::FunctionSet callees;
        callgraph->getCallees(CS,callees);
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
                            alreadyTaintedObjVFGNode.insert(objVFGNode->getId());
                        }
                    }
                }
                LLVM_DEBUG(dbgs() << "----------------------------------------------------\n");
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
            if (alreadyTaintedObjVFGNode.find(AVF->getId()) != alreadyTaintedObjVFGNode.end())
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
                        vNode = getVFGNodeFromValue(pag, svfg, I);
                        LLVM_DEBUG(dbgs() << "ADDR: " << *vNode << "\n");

                        /// update the unsafe svf node
                        UnsafeSVFNodes.insert(vNode->getId());

                        LLVM_DEBUG(dbgs() << "*********UPDATE TAINT LIST START: " << vNode->getId() << "*********\n");
                        updateTaintList(svfg, vNode);
                        LLVM_DEBUG(dbgs() << "*********UPDATE TAINT LIST END***********\n");

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
                        UnsafeSVFNodes.insert(getVFGNodeFromValue(pag, svfg, Item->getValue())->getId());
                    }
                }
            }
        }
    }

    LLVM_DEBUG(dbgs() << "Checking whether tainted nodes are bounded using heuristics...\n");

    /// write some logs to file
    string dppLog = "#################### RULE 1 #########################\n";

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

                    /// getting all the reachable nodes reachable from either the svf node or its param nodes
                    auto reachableNodes = getReachableNodes(SVFNode, pag, svfg);

                    /// getting all the cfg paths ended up with the svf node
                    auto cmpPaths = getCmpPaths(SVFNode->getICFGNode());
                    auto totalPaths = cmpPaths.size();
                    auto totalRelevantPaths = 0; ///paths with data dependent cmp instructions

                    /// computing the number of paths that have relevant compare instructions
                    for (auto path: cmpPaths) {
                        for (auto node: path) {
                            if (node->getNodeKind() == ICFGNode::IntraBlock) {
                                if (auto *IBN = SVFUtil::dyn_cast<IntraBlockNode>(node)) {
                                    if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(IBN->getInst())) {

                                        if (I->getOpcode() == Instruction::ICmp ||
                                            I->getOpcode() == Instruction::FCmp) {

                                            if (checkCommonAncestor(node->getVFGNodes(), reachableNodes)) {
                                                totalRelevantPaths++;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    /// getting the compare instruction to see if the data pointer is bounded
                    auto cmpNodes = getCmpInstructions(SVFNode->getICFGNode());

                    BoundApproxType BT;
                    auto bounds = approximateBound(cmpNodes);
                    if (bounds.first != LONG_MAX && bounds.second != LONG_MIN)
                        BT = BoundApproxType::CONSTANT;
                    else
                        BT = BoundApproxType::VARIABLE;

                    /// computing pointer score for prioritization
                    auto score = computePointerScore(totalPaths, totalRelevantPaths, BT);

                    dppLog += SVFNode->toString() + "\n";
                    dppLog += "Yes, " + std::to_string(totalPaths) + ", " + std::to_string(totalRelevantPaths) + ", "
                            + std::to_string(BT) + " (" + std::to_string(bounds.first) + ", "
                            + std::to_string(bounds.second) + "), " + std::to_string(score) + "\n";
                    dppLog += "--------------------------------------------------------------\n";

                    /// storing the score so that we can pass the result
                    R.try_emplace(I, score);
                }
            }
        }
    }

    dppLog += "##################################################\n\n\n";
    DPP::writeDPPLogsToFile(dppLog);

    return R;
}


AnalysisKey DPPRule1G::Key;
[[maybe_unused]] const char DPPRule1G::RuleName[] = "DPPRule1G";

DPPRule1G::Result DPPRule1G::run(Module &M, AnalysisManager<Module> &AM) {
    Result Result{};

    auto R = AM.getResult<SVFInitPass>(M);

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *CallGraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    auto DPointers = Rule1Global(pag, CallGraph, svfg);
    for(auto Ptr: DPointers) {
        Result.PrioritizedPtrMap.try_emplace(Ptr.getFirst(), Ptr.getSecond());
    }

    return Result;
}

raw_ostream &DPPRule1GResult::print(raw_ostream &OS) const {
    OS << "Prioritized data objects by Rule 1:\n";
    for (auto Ptr : PrioritizedPtrMap) {
        auto *const I = Ptr.getFirst();
        auto score = Ptr.getSecond();
        OS << *I << ", score = " << score << "\n";
    }
    return OS;
}
