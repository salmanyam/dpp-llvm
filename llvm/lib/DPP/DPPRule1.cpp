//
// Created by salman on 3/8/21.
//
#include <queue>

#include "llvm/DPP/DPPRule1.h"
#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

void Rule1Global(PAG *pag, PTACallGraph* callgraph, SVFG *svfg);

using namespace llvm;
using namespace llvm::DPP;
using namespace std;
using namespace SVF;

#define DEBUG_TYPE "DPPRule1"

// for manually inserting input reading functions
InputFunctionMap InputFunctions;
InputFunction IF_scanf = {"__isoc99_scanf", 1, false};
InputFunction IF_fscanf = {"__isoc99_fscanf", 2, false};
InputFunction IF_freaduntil = {"freaduntil", 0, false};
InputFunction IF_fread = {"fread", 0, false};
InputFunction IF_read = {"read", 0, false};
InputFunction IF_fgets = {"fgets", 0, false};
InputFunction IF_recv = {"recv", 0, false};

//todo: marking the return type as input dependent


// for tracking the SVFG nodes that are dependent on input
NodeSet InputSVFGNodes;
NodeSet TaintedSVFNodes;
NodeSet UnsafeSVFNodes;
DenseMap<const VFGNode *, bool> SafeSVFNodes;


void Rule1Init() {
    errs() << "Initializing input functions for Rule 7\n";

    // clear any SVF nodes from the dataset
    InputSVFGNodes.clear();
    TaintedSVFNodes.clear();
    UnsafeSVFNodes.clear();

    // clear the SVF nodes from dataset
    SafeSVFNodes.clear();

    // Insert the input functions
    InputFunctions.try_emplace(IF_scanf.name, &IF_scanf);
    InputFunctions.try_emplace(IF_fscanf.name, &IF_fscanf);
    InputFunctions.try_emplace(IF_freaduntil.name, &IF_freaduntil);
    InputFunctions.try_emplace(IF_fread.name, &IF_fread);
    InputFunctions.try_emplace(IF_read.name, &IF_read);
    InputFunctions.try_emplace(IF_fgets.name, &IF_fgets);
    InputFunctions.try_emplace(IF_recv.name, &IF_recv);

}

bool isDataPointer(Type *T) {
    if (T && T->isPointerTy() && !T->isFunctionTy()) {
        return true;
    }
    return false;
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
 * Mark all the nodes in the SVFG as input dependent along the path
 * of all the uses of a definition of a value along value-flow graph (VFG)
 */
void markInputNodesOnSVFG(const VFGNode* vNode) {
    FIFOWorkList<const VFGNode*> worklist;
    Set<const VFGNode*> visited;
    visited.insert(vNode);
    worklist.push(vNode);

    /// Traverse along VFG
    while (!worklist.empty())
    {
        const VFGNode* vNode = worklist.pop();
        //errs() << *vNode << "\n";
        // insert the input nodes
        InputSVFGNodes.insert(vNode->getId());
        errs() << *vNode << "\n";

        for (VFGNode::const_iterator it = vNode->OutEdgeBegin(), eit =
                vNode->OutEdgeEnd(); it != eit; ++it) {
            VFGEdge *edge = *it;
            VFGNode *successorNode = edge->getDstNode();

            // if not visited, then mark as visted and insert into worklist
            if (visited.find(successorNode) == visited.end()) {
                visited.insert(successorNode);
                worklist.push(successorNode);
            }
        }

        // if the VFG node is a store and if the first param is
        // stored on the second param of a store instruction, then
        // include the second param as well.
        if (vNode->getNodeKind() == VFGNode::VFGNodeK::Store) {
            //errs() << "store related start---------------------\n";
            //errs() << *vNode << "\n";
            const StoreVFGNode *SVFG = SVFUtil::dyn_cast<StoreVFGNode>(vNode);
            //errs() << *SVFG->getInst() << "\n";

            // we know the instruction is store, so get the first and second operand
            // auto Operand1 = SVFG->getInst()->getOperand(0);
            auto Operand2 = SVFG->getInst()->getOperand(1);

            for (VFGNode::const_iterator it = vNode->InEdgeBegin(), eit =
                    vNode->InEdgeEnd(); it != eit; ++it) {
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
                    if(StmtVFGNode *Smt = SVFUtil::dyn_cast<StmtVFGNode>(predNode)) {
                        // Insert this node if the following is true, i.e.,
                        // user dependent variable is being stored in operand2
                        if (Smt->getInst() == Operand2) {
                            //errs() << *Smt << "\n";
                            visited.insert(Smt);
                            worklist.push(Smt);
                        }
                    }
                }
            }
            //errs() << "store related end---------------------\n";
        }

    }

    /*
    /// Collect all LLVM Values
    for(Set<const VFGNode*>::const_iterator it = visited.begin(), eit = visited.end(); it!=eit; ++it)
    {
        const VFGNode* node = *it;
        /// can only query VFGNode involving top-level pointers (starting with % or @ in LLVM IR)
        /// PAGNode* pNode = vfg->getLHSTopLevPtr(node);
        //Value* val = pNode->getValue();
        errs() << *node << "\n";
    }*/
}

/*!
 * Look for all the nodes in the SVFG starting from an address taken node.
 * If a conditional statement is found, mark the starting address taken node
 * as potentially safe.
 */
bool lookForCondition(const VFGNode* vNode) {
    FIFOWorkList<const VFGNode*> worklist;
    Set<const VFGNode*> visited;
    visited.insert(vNode);
    worklist.push(vNode);

    /// Traverse along VFG
    while (!worklist.empty())
    {
        const VFGNode* vNode = worklist.pop();
        //errs() << *vNode << "\n";
        // insert the input nodes
        //InputSVFGNodes.insert(vNode->getId());
        errs() << *vNode << "\n";
        if (vNode->getNodeKind() == VFGNode::VFGNodeK::Cmp) {
            return true;
        }

        //if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(AVFG->getInst())) {

        for (VFGNode::const_iterator it = vNode->OutEdgeBegin(), eit =
                vNode->OutEdgeEnd(); it != eit; ++it) {
            VFGEdge *edge = *it;

            // do not follow the indirectly edge, discard it for now
            // todo: visit here to think about the indirect edge
            if (edge->isIndirectVFGEdge())
                continue;

            VFGNode *successorNode = edge->getDstNode();

            // if not visited, then mark as visted and insert into worklist
            if (visited.find(successorNode) == visited.end()) {
                visited.insert(successorNode);
                worklist.push(successorNode);
            }
        }
    }

    return false;
}

/*!
 * This function traverse the SVF graph and collect taint list
 * by following nodes that are dependent on user input
 */
NodeSet updateTaintList(SVFG *svfg, const VFGNode* arg_vNode) {

    NodeSet m_PointsTo;

    FIFOWorkList<const VFGNode*> worklist;
    Set<const VFGNode*> visited;
    worklist.push(arg_vNode);

    /// Traverse along VFG
    while (!worklist.empty())
    {
        const VFGNode* vNode = worklist.pop();
        errs() << "TAINTING: " << *vNode << "\n";
        TaintedSVFNodes.insert(vNode->getId());

        // if an allocation site is found, then propagate the input
        // dependency to all successor nodes
        if (vNode->getNodeKind() == VFGNode::VFGNodeK::Addr) {
            UnsafeSVFNodes.insert(vNode->getId());
            //errs() << "\n\nFound AddrVFGNode = " << *vNode << "\n\n";
            //errs() << "****************PROPAGATION START******************\n";
            //markInputNodesOnSVFG(vNode);
            //errs() << "****************PROPAGATION END******************\n";
        }

        //errs() << "Total outgoing edges = " << vNode->getOutEdges().size() << "\n";

        for (auto it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd(); it != eit; ++it) {
            VFGEdge *edge = *it;

            // skip the indirect relationship for finding allocation site for input dependency
            //if (edge->isIndirectVFGEdge())
            //   continue;

            VFGNode *dstNode = edge->getDstNode();

            if (visited.find(dstNode) == visited.end()) {
                visited.insert(dstNode);
                worklist.push(dstNode);
            }
        }

        // if the VFG node is a store and if the first param is
        // stored on the second param of a store instruction, then
        // include the second param as well.
        if (vNode->getNodeKind() == VFGNode::VFGNodeK::Store) {
            const auto *SVFG = SVFUtil::dyn_cast<StoreVFGNode>(vNode);

            // we know the instruction is store, so get the second operand
            auto Operand2 = SVFG->getInst()->getOperand(1);

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
                            //errs() << *Smt << "\n";
                            visited.insert(Smt);
                            worklist.push(Smt);

                            // get the points-to set
                            NodeID pNodeId = svfg->getPAG()->getValueNode(Operand2);
                            const NodeBS& pts = svfg->getPTA()->getPts(pNodeId);
                            for (unsigned int pt : pts)
                            {
                                PAGNode* targetObj = svfg->getPAG()->getPAGNode(pt);
                                if(targetObj->hasValue())
                                {
                                    //errs() << *targetObj << "\n";
                                    m_PointsTo.insert(targetObj->getId());
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return m_PointsTo;
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

auto getCmpInstructions(const ICFGNode *icfgNode) {
    Set<const ICFGNode *> cmpNodes;
    bool isConstant;

    FIFOWorkList<const ICFGNode*> worklist;
    Set<const ICFGNode*> visited;
    worklist.push(icfgNode);

    /// Traverse along VFG
    while (!worklist.empty()) {
        const ICFGNode *node = worklist.pop();
        //errs() << "ICFG: " << *node << "\n";

        if (node->getNodeKind() == ICFGNode::IntraBlock) {
            if (auto *IBN = SVFUtil::dyn_cast<IntraBlockNode>(node)) {
                if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(IBN->getInst())) {
                    /// If we found a cmp instruction, insert it into our list
                    if (I->getOpcode() == Instruction::ICmp || I->getOpcode() == Instruction::FCmp) {
                        //errs() << *I << "\n";
                        if (cmpNodes.size() >= BACKWARD_CMPS)
                            continue;

                        cmpNodes.insert(node);
                    }
                    /// determine if constant value is being stored, if so then skip the successor of this node
                    /*else if (I->getOpcode() == Instruction::Store) {
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
                    if(!SVFUtil::isa<Constant>(P->getValue())) {
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
        for (ICFGNode::const_iterator it = node->InEdgeBegin(), eit =
                node->InEdgeEnd(); it != eit; ++it) {
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
    bool isConstant, haveNewNode;

    queue<vector<const ICFGNode *>> worklist;
    vector<const ICFGNode *> path;
    path.push_back(icfgNode);
    worklist.push(path);

    /// Traverse along VFG
    while (!worklist.empty()) {
        auto curPath = worklist.front();
        worklist.pop();

        toReturn.push_back(curPath);
        //for (auto item: curPath)
        //    errs() << item->getId() << " ";
        //errs() << "\n";

        auto node = curPath[curPath.size()-1];
        //errs() << "ICFG: " << *node << "\n";
        //errs() << node->getId() << " ";

        if (node->getNodeKind() == ICFGNode::IntraBlock) {
            if (auto *IBN = SVFUtil::dyn_cast<IntraBlockNode>(node)) {
                if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(IBN->getInst())) {
                    /// If we found a cmp instruction, insert it into our list
                    if (I->getOpcode() == Instruction::ICmp || I->getOpcode() == Instruction::FCmp) {
                        //errs() << *I << "\n";
                        if (getNumCmpsInPath(curPath) >= BACKWARD_CMP_PATHS)
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

    errs() << "Total traversed = " << visited.size() << "\n";

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

auto computeDPPScore(Set<const ICFGNode*> cmpNodes, vector<vector<const ICFGNode *>> cmpPaths) {
    int32_t score = 0;

    /// if no cmp instructions, then update score and return
    if (cmpNodes.size() == 0) {
        score += NO_CMP;
        return score;
    }

    /// if there is at least one cmp instruction
    score += YES_CMP;

    /// get the lower and upper constant bounds, LONG_MAX or LONG_MIN if not present
    auto bounds = approximateBound(cmpNodes);
    ///bound.first = lower bound, bound.second == upper bound
    errs() << "lower bound = " << bounds.first << ", upper bound = " << bounds.second << "\n";

    // if no bound is present
    if (bounds.first == LONG_MAX && bounds.second == LONG_MIN) {
        score += HAVE_VARIABLE_BOUND;
    } else {
        score += HAVE_CONSTANT_BOUND;
    }

    return score;
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
    if (BT == BoundApproxType::NONE)
        score += HAVE_NONE_BOUND;
    else if (BT == BoundApproxType::VARIABLE)
        score += HAVE_VARIABLE_BOUND;
    else
        score += HAVE_CONSTANT_BOUND;

    /// score based safe vs unsafe uses
    //todo

    return score;
}

bool score_compare(DPPScore *a, DPPScore *b) {
    return a->Score < b->Score;
};

void Rule1Global(PAG *pag, PTACallGraph* callgraph, SVFG *svfg) {
    // Rule 7: data pointers dependent on external source
    // External sources in this case are function calls

    // Initialize the manually inserted input reading functions
    Rule1Init();

    //svfg->dump("/home/salman/DPP/data/rules-minimal/rule1/rule1-svfg", true);
    //svfg->dump("/home/salman/DPP/data/ghttpd-1.4/ghttpd-svfg", true);

    /// construct the ICFG graph to get the compare instructions to approximate whether a variable or
    /// memory allocation is bounded or not
    ICFG *icfg = pag->getICFG();
    //icfg->dump("/home/salman/DPP/data/rules-minimal/rule1/rule1-icfg2", true);
    errs() << "First node = " << *icfg->begin()->second << "\n";
    errs() << "Total nodes = " << icfg->getTotalNodeNum() << "\n";

    /// get the entry block using global block node, the entry block is the parent of global block
    auto gBN = icfg->getGlobalBlockNode();
    ICFGNode *entryBlock = nullptr;
    for (auto it = gBN->InEdgeBegin(), eit = gBN->InEdgeEnd(); it != eit; ++it) {
        ICFGEdge *edge = *it;
        entryBlock = edge->getSrcNode();
        break;
    }
    assert(entryBlock != nullptr && "Entry block node in ICFG is NULL, can't assign depth!");

    /// assigned depth to each node of the ICFG to determine the compare for bound check because
    /// there might be multiple compare instructions for a single variable
    auto DepthMap = assignDepth(entryBlock, 0);

    /// track the points-to set of input function arguments
    NodeSet m_PointsTo;

    /// Get all call sites, filter input reading functions, and
    /// mark the nodes dependent on input reading functions
    for(const CallBlockNode *CS: pag->getCallSiteSet()) {
        /// get the names of the calle functions to filter input functions
        PTACallGraph::FunctionSet callees;
        callgraph->getCallees(CS,callees);
        for(auto func : callees) {
            //errs() << func->getName() << "\n";

            if (isInputReadingFunction(func->getName())) {
                errs() << func->getName() << " " << getInputArgStart(func->getName()) << "\n";
                unsigned int paramIndex = 0;
                for (auto P: CS->getActualParms()) {
                    /// skip until the meaning params come
                    /// todo: may be consider all params would be easier to construct the input functions?
                    if (paramIndex < getInputArgStart(func->getName())) {
                        paramIndex++;
                        continue;
                    }

                    errs() << "Param " << paramIndex << ": " << *P << "\n";
                    paramIndex++;

                    /// skip constant type parameter
                    if(SVFUtil::isa<Constant>(P->getValue()))
                        continue;

                    errs() << "SVFG Node ID = " << *svfg->getDefSVFGNode(P) << "\n";

                    /// update the taint list with all successors starting from the svf node of param P
                    auto r_PointsTo = updateTaintList(svfg, svfg->getDefSVFGNode(P));

                    // get the points-to set
                    NodeID pNodeId = svfg->getPAG()->getValueNode(P->getValue());
                    const NodeBS& pts = svfg->getPTA()->getPts(pNodeId);
                    for (unsigned int pt : pts)
                    {
                        PAGNode* targetObj = svfg->getPAG()->getPAGNode(pt);
                        if(targetObj->hasValue())
                        {
                            //errs() << *targetObj << "\n";
                            m_PointsTo.insert(targetObj->getId());
                        }
                    }

                    // merge two points-to sets
                    for(auto item: r_PointsTo) {
                        m_PointsTo.insert(item);
                    }

                }
                errs() << "----------------------------------------------------\n";
            }
        }
        // errs() << "--------------------\n";
    }

    // Each invocation of updateTaintList() can have new points-to set
    // So until the points-to set is empty, invoke the updateTaintList() function
    NodeSet m_NewPointsTo;
    while (!m_PointsTo.empty()) {
        errs() << "Total points-to = " << m_PointsTo.size() << "\n";
        for (auto item: m_PointsTo)
            errs() << "Points TO ID = " << item << "\n";

        m_NewPointsTo.clear();
        for (auto SV = svfg->begin(); SV != svfg->end(); ++SV) {
            VFGNode *V = SV->second;
            if (auto *AVF = SVFUtil::dyn_cast<AddrVFGNode>(V)) {
                /// to discard the function and global constant type nodes
                /// todo: revisit here regarding ID 0
                if (AVF->getICFGNode()->getId() == 0)
                    continue;

                // update the taint list for points-to set populated in the input arguments
                if (m_PointsTo.find(AVF->getPAGSrcNodeID()) != m_PointsTo.end() ||
                    m_PointsTo.find(AVF->getPAGDstNodeID()) != m_PointsTo.end()) {

                    /// update the unsafe svf node
                    UnsafeSVFNodes.insert(AVF->getId());

                    errs() << "****************UPDATE TAINT LIST START: " << AVF->getId() << "******************\n";
                    auto r_PointsTo = updateTaintList(svfg, AVF);
                    errs() << "****************UPDATE TAINT LIST END******************\n";

                    for (auto item: r_PointsTo)
                        m_NewPointsTo.insert(item);
                }
            }
        }
        m_PointsTo.clear();

        for (auto item: m_NewPointsTo)
            m_PointsTo.insert(item);
    }


    /// for each address taken memory allocation nodes, i.e., Address SVF nodes
    /// add the address SVF nodes to UnsafeSVFNodes if they depend on the tainted nodes
    uint64_t totalDataPointers = 0; // to count the total number of data pointers
    for(auto SV = svfg->begin(); SV != svfg->end(); ++SV) {
        VFGNode *V = SV->second;
        if (auto *AVF = SVFUtil::dyn_cast<AddrVFGNode>(V)) {
            /// to discard the function and global constant type nodes
            /// todo: revisit here regarding ID 0
            if (AVF->getICFGNode()->getId() == 0)
                continue;


            /// count total number of data pointers
            Type *PTy = nullptr;
            if (const auto *I = SVFUtil::dyn_cast<Instruction>(AVF->getInst())) {
                if (I->getOpcode() == Instruction::Alloca) {
                    if (const auto *AI = SVFUtil::dyn_cast<AllocaInst>(I)) {
                        PTy = AI->getAllocatedType();
                    }
                } else if (I->getOpcode() == Instruction::Call) {
                    if (const auto *CI = SVFUtil::dyn_cast<CallInst>(I)) {
                        PTy = CI->getCalledFunction()->getReturnType();
                    }
                }
            }
            if (PTy && isDataPointer(PTy)) {
                totalDataPointers++;
            }

            /// to check whether a node is being used as an argument to an input function
            /// but the node does not have any dependency on other tainted nodes
            /// that means the node is a source node
            //bool isSourceNode = true; ///we still need to track it

            /// All the leftover nodes are instruction type
            if (const auto *I = SVFUtil::dyn_cast<Instruction>(AVF->getInst())) {
                // skip the alloca instruction because its operand does not have any associated SVF node
                if (I->getOpcode() == Instruction::Alloca)
                    continue;

                // loop through all the operands and update the taint list if
                // any operands of the instruction depend on user input
                for (auto Op = I->op_begin(); Op != I->op_end(); ++Op) {
                    const VFGNode* vNode = getVFGNodeFromValue(pag, svfg, Op->get());
                    /// if an operand is tainted, update the tainted list
                    if (TaintedSVFNodes.find(vNode->getId()) != TaintedSVFNodes.end()) {
                        vNode = getVFGNodeFromValue(pag, svfg, I);
                        errs() << "ADDR: " << *vNode << "\n";

                        /// update the unsafe svf node
                        UnsafeSVFNodes.insert(vNode->getId());

                        /// this is not a source node because the node's argument depends on user input
                        //isSourceNode = false;

                        errs() << "****************UPDATE TAINT LIST START: " << vNode->getId() << "******************\n";
                        updateTaintList(svfg, vNode);
                        errs() << "****************UPDATE TAINT LIST END******************\n";

                        /// no need to look further because vNode is for the whole instruction, other operand will
                        /// also have the same instruction
                        break;
                    }
                }
            }

            /// remove a source AddrVFG node from the UnsafeSVFNodes if it's present there
            //if ( isSourceNode && UnsafeSVFNodes.find(AVF->getId()) != UnsafeSVFNodes.end())
            //    UnsafeSVFNodes.erase(AVF->getId());
        }
    }

    //for (auto ID: TaintedSVFNodes) {
    //errs() << "ID = " << ID << " " << *svfg->getSVFGNode(ID) << "\n";
    //}

    /*
    bool isFoundCondition = false;
    for(auto SV = svfg->begin(); SV != svfg->end(); ++SV) {
        //errs() << SV->first << " " << *SV->second << "\n";
        VFGNode *V = SV->second;
        if (AddrVFGNode *AVFG = SVFUtil::dyn_cast<AddrVFGNode>(V)) {
            // to discard the function and global constant type nodes
            if (AVFG->getICFGNode()->getId() == 0) //todo: revisit here regarding ID 0
                continue;
            errs() << *AVFG << "\n";

            // All the leftover nodes are instruction type
            if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(AVFG->getInst())) {
                //errs() << *I << "\n";
                //errs() << I->getNumOperands() << "\n";
                const VFGNode* vNode = getVFGNodeFromValue(pag, svfg, I);
                //errs() << "VFGNode = " << *vNode << "\n";
                // look for condition starting from vNode
                //errs() << "****************LOOKUP CONDITION START******************\n";
                //isFoundCondition = lookForCondition(vNode);
                //errs() << "****************LOOKUP CONDITION END******************\n";
                //errs() << "Found condition = " << isFoundCondition << "\n";
                //if (isFoundCondition)
                    //SafeSVFNodes.try_emplace(vNode, isFoundCondition);
            }
        }
    }*/

    //for (auto VFG : SafeSVFNodes)
    //   errs() << *VFG.getFirst() << ", found condition = " << VFG.getSecond() << "\n";


    Set<DPPScore *> PtrScores;
    DPPScore *PtrScore = NULL;
    uint64_t inputDependentDP = 0;
    for (auto ID: UnsafeSVFNodes) {
        const auto SVFNode = svfg->getSVFGNode(ID);
        uint32_t nodeDepth = DepthMap.lookup(SVFNode->getICFGNode()->getId());
        errs() << *SVFNode << ", Depth = " << DepthMap.lookup(SVFNode->getICFGNode()->getId()) << "\n";
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
            }
        }
        if (PTy && isDataPointer(PTy)) {
            //errs() << "This node has unsafe data pointer!" << "\n";

            inputDependentDP++;

            /// getting all the reachable nodes reachable from either the svf node or its param nodes
            auto reachableNodes = getReachableNodes(SVFNode, pag, svfg);

            /// getting all the cfg paths ended up with the svf node
            auto cmpPaths = getCmpPaths(SVFNode->getICFGNode());
            auto totalPaths = cmpPaths.size();
            auto totalRelevantCmpPaths = 0; ///paths with data dependent cmp instructions

            for (auto path: cmpPaths) {
                for (auto node: path) {
                    if (node->getNodeKind() == ICFGNode::IntraBlock) {
                        if (auto *IBN = SVFUtil::dyn_cast<IntraBlockNode>(node)) {
                            if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(IBN->getInst())) {
                                if (I->getOpcode() == Instruction::ICmp || I->getOpcode() == Instruction::FCmp) {
                                    if (checkCommonAncestor(node->getVFGNodes(), reachableNodes)) {
                                        totalRelevantCmpPaths++;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            //errs() << "Number of paths = " << totalPaths << ", relevant cmp paths = " << totalRelevantCmpPaths << "\n";

            /// getting the compare instruction to see if the data pointer is bounded
            auto cmpNodes = getCmpInstructions(SVFNode->getICFGNode());
            Set<const ICFGNode *> filteredCmpNodes;

            for (auto item: cmpNodes) {
                ///skip all the cmps that come after the current node
                if (DepthMap.lookup(item->getId()) > nodeDepth)
                    continue;

                if (checkCommonAncestor(item->getVFGNodes(), reachableNodes)) {
                    //errs() << *item << ", Depth = " << DepthMap.lookup(item->getId()) << "\n";
                    filteredCmpNodes.insert(item);
                }
            }

            BoundApproxType BT = BoundApproxType::NONE;
            auto bounds = approximateBound(cmpNodes);
            if (bounds.first != LONG_MAX && bounds.second != LONG_MIN)
                BT = BoundApproxType::CONSTANT;
            else
                BT = BoundApproxType::VARIABLE;


            //todo: safe usage score if possible

            //auto score = computeDPPScore(filteredCmpNodes, cmpPaths);
            auto score = computePointerScore(totalPaths, totalRelevantCmpPaths, BT);


            PtrScore = static_cast<DPPScore *>(malloc(sizeof(DPPScore)));
            PtrScore->ID = ID;
            PtrScore->Score = score;
            PtrScores.insert(PtrScore);

            errs() << ID << ", Yes, " << totalPaths << ", " << totalRelevantCmpPaths << ", " << BT << "(" <<
                   bounds.first << ", " << bounds.second << "), " << score << "\n";
        }
        else {
            //errs() << "This node is safe!" << "\n";
            errs() << ID << ", No, -, -, -, -" << "\n";
        }
    }

    errs() << "Total pointers = " << totalDataPointers << ", input dependent pointers = " << inputDependentDP << "\n";

    //for (auto item: PtrScores)
    //  errs() << item->ID << " " << item->Score << "\n";

    /*
    // for each address taken object, determine the sensitive data pointers
    for(auto SV = svfg->begin(); SV != svfg->end(); ++SV) {
        VFGNode *V = SV->second;
        if (AddrVFGNode *AVFG = SVFUtil::dyn_cast<AddrVFGNode>(V)) {
            //errs() << AVFG->getPAGEdge()->getValue()->getName() << "\n";

            // to discard the svfg nodes that contains the whole functions
            if (AVFG->getICFGNode()->getId() == 0) //todo: revisit here regarding ID 0
                continue;

            auto Ty = AVFG->getPAGEdge()->getValue()->getType();
            if (Ty && Ty->isPointerTy()) {
                errs() << *AVFG << "\n";
                errs() << *AVFG->getPAGEdge()->getValue()->getType() << "\n";
                errs() << *Ty->getPointerElementType() << "\n";
                if (isDataPointer(Ty->getPointerElementType())) {
                    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(AVFG->getPAGEdge()->getValue()));
                    const VFGNode* vNode = svfg->getDefSVFGNode(pNode);

                    if (InputSVFGNodes.find(vNode->getId()) != InputSVFGNodes.end()) {
                        errs() << "Sensitive data pointer = " << *AVFG->getPAGEdge()->getValue() << "\n";
                        //errs() << "Found sensitive data pointer = " << *vNode << "\n";
                    } else {
                        errs() << "Non-sensitive data pointer = " << *AVFG->getPAGEdge()->getValue() << "\n";
                    }

                }
            }
        }

    }
    */
}


AnalysisKey DPPRule1G::Key;
[[maybe_unused]] const char DPPRule1G::RuleName[] = "DPPRule1G";

DPPRule1G::Result DPPRule1G::run(Module &M, AnalysisManager<Module> &AM) {
  Result Result {};

  SVFModule *SVFModule = LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(M);

  PAGBuilder Builder;
  PAG *PAG = Builder.build(SVFModule);

  Andersen *Andersen = AndersenWaveDiff::createAndersenWaveDiff(PAG);

  PTACallGraph *Callgraph = Andersen->getPTACallGraph();

//   PTACallGraphNode *CGNode = nullptr;

//   for (auto CG = Callgraph->begin(), end = Callgraph->end(); CG != end; ++CG) {
//       CGNode = CG->second;
//       if (CGNode->getFunction()->getName() == "malloc")
//           break;
//   }

//   ICFG* ICFG = PAG->getICFG();

//   for (auto it = PAG->getCallSiteRets().begin(),
//             end = PAG->getCallSiteRets().end();
//        it != end; ++it) {
//     const RetBlockNode *cs = it->first;
//   }

  SVFGBuilder SVFGBuilder;
  SVFG *SVFG = SVFGBuilder.buildFullSVFGWithoutOPT(Andersen);

  Rule1Global(PAG, Callgraph, SVFG);

  return Result;
}

raw_ostream &DPPRule1GResult::print(raw_ostream &OS) const {
  OS << "not implemented\n\n";
  return OS;
}
