//
// Created by salman on 7/5/21.
//

#include <queue>
#include <chrono>

#include "llvm/DPP/SVFInitPass.h"
#include "llvm/DPP/DPPUtils.h"
#include "llvm/DPP/DPPRule1.h"
#include "llvm/DPP/DPPRule9.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

#include "SABER/SaberCheckerAPI.h"
#include "SABER/LeakChecker.h"

#define DEBUG_TYPE "DPPRule9"

using namespace llvm;
using namespace llvm::DPP;
using namespace SVF;


[[maybe_unused]] const char DPPRule9G::RuleName[] = "DPPRule9G";
AnalysisKey DPPRule9G::Key;


auto DPPRule9G::getVFGNodeFromValue(SVF::PAG *pag, SVF::SVFG *svfg, const Value *val) {
    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(val));
    const VFGNode* vNode = svfg->getDefSVFGNode(pNode);

    return vNode;
}


auto DPPRule9G::assignDepth(const ICFGNode *firstNode, unsigned long long depth) {
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


auto DPPRule9G::getReachableNodes(const VFGNode* vNode, PAG *pag, SVFG *svfg) {
    set<uint32_t> vNodeIDs;

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


int DPPRule9G::getNumCmpsInPath(vector<const ICFGNode *> P) {
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


auto DPPRule9G::getCmpPaths(const ICFGNode *icfgNode) {
    vector<vector<const ICFGNode *>> toReturn;
    bool haveNewNode;

    auto numBackwardCmpPaths = getNumBackwardCmpPaths();
    auto numNodeInPathToConsider = getNumNodesInPath();

    queue<vector<const ICFGNode *>> worklist;
    vector<const ICFGNode *> path;
    path.push_back(icfgNode);
    worklist.push(path);

    /// Traverse along ICFG
    while (!worklist.empty()) {
        auto curPath = worklist.front();
        worklist.pop();

        toReturn.push_back(curPath);
        //for (auto item: curPath)
        //    errs() << item->getId() << " ";
        //errs() << "\n";

        if (curPath.size() > numNodeInPathToConsider)
            continue;

        if (getNumCmpsInPath(curPath) >= numBackwardCmpPaths)
            continue;

        auto node = curPath[curPath.size()-1];
        //errs() << "ICFG: " << *node << "\n";
        //errs() << node->getId() << " ";

        /*if (node->getNodeKind() == ICFGNode::IntraBlock) {
            if (auto *IBN = SVFUtil::dyn_cast<IntraBlockNode>(node)) {
                if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(IBN->getInst())) {
                    /// If we found a cmp instruction, insert it into our list
                    if (I->getOpcode() == Instruction::ICmp || I->getOpcode() == Instruction::FCmp) {
                        //errs() << *I << "\n";
                        if (getNumCmpsInPath(curPath) >= 2) //BACKWARD_CMP_PATHS
                            continue;
                    }
                }
            }
        }*/

        /// for all incoming edges, get the source nodes and look for them
        haveNewNode = false;
        for (auto it = node->InEdgeBegin(), eit = node->InEdgeEnd(); it != eit; ++it) {
            ICFGEdge *edge = *it;
            ICFGNode *srcNode = edge->getSrcNode();

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

bool DPPRule9G::checkCommonAncestor(list<const VFGNode *> vNodes, set<uint32_t> toCompare) {
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


auto DPPRule9G::getCmpInstructions(const ICFGNode *icfgNode) {
    Set<const ICFGNode *> cmpNodes;

    auto numBackwardCmps = getNumBackwardCmps();

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
                        if (cmpNodes.size() >= numBackwardCmps)
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


DPPRule9G::Result DPPRule9G::run(Module &M, AnalysisManager<Module> &AM) {
    Result Result {};

    using std::chrono::high_resolution_clock;
    using std::chrono::duration;
    duration<double, std::milli> runtime_ms;

    auto R = AM.getResult<SVFInitPass>(M);

    LLVM_DEBUG(dbgs() << "Starting rule 9...\n");

    PAG *pag = R.SVFParams.pag;
    PTACallGraph *CallGraph = R.SVFParams.CallGraph;
    SVFG *svfg = R.SVFParams.svfg;

    LLVM_DEBUG(dbgs() << "Finding the allocation sites...\n");

    auto t1 = high_resolution_clock::now();

    Set<const SVFGNode*> SVFGAllocationNodeSet;
    for(PAG::CSToRetMap::iterator it = pag->getCallSiteRets().begin(),
                eit = pag->getCallSiteRets().end(); it!=eit; ++it) {
        const RetBlockNode *cs = it->first;
        /// if this callsite return reside in a dead function then we do not care about its leaks
        /// for example instruction p = malloc is in a dead function, then program won't allocate this memory
        if (SVFUtil::isPtrInDeadFunction(cs->getCallSite()))
            continue;

        PTACallGraph::FunctionSet callees;
        CallGraph->getCallees(cs->getCallBlockNode(),callees);
        for(PTACallGraph::FunctionSet::const_iterator cit = callees.begin(), ecit = callees.end(); cit!=ecit; cit++)
        {
            const SVFFunction* fun = *cit;

            if(SaberCheckerAPI::getCheckerAPI()->isMemAlloc(fun)) {
                FIFOWorkList<const CallBlockNode*> worklist;
                NodeBS visited;
                worklist.push(it->first->getCallBlockNode());

                while (!worklist.empty()) {
                    const CallBlockNode *cs = worklist.pop();
                    const RetBlockNode *retBlockNode = pag->getICFG()->getRetBlockNode(cs->getCallSite());
                    const PAGNode *pagNode = pag->getCallSiteRet(retBlockNode);
                    const SVFGNode *node = svfg->getDefSVFGNode(pagNode);
                    if (visited.test(node->getId()) == 0)
                        visited.set(node->getId());
                    else
                        continue;


                    /// determine whether a SVFGNode n is in a allocation wrapper function,
                    /// if so, return all SVFGNodes which receive the value of node n
                    Set<const CallBlockNode*> csIdSet;
                    bool wrapperFlag = false;
                    bool reachFunExit = false;

                    FIFOWorkList<const SVFGNode*> worklist2;
                    worklist2.push(node);
                    NodeBS visited2;

                    while (!worklist2.empty())
                    {
                        const SVFGNode* node2  = worklist2.pop();

                        if(visited2.test(node2->getId())==0)
                            visited2.set(node2->getId());
                        else
                            continue;

                        for (SVFGNode::const_iterator it2 = node2->OutEdgeBegin(), eit2 =
                                node2->OutEdgeEnd(); it2 != eit2; ++it2)
                        {
                            const SVFGEdge* edge = (*it2);
                            assert(edge->isDirectVFGEdge() && "the edge should always be direct VF");
                            // if this is a call edge
                            if(edge->isCallDirectVFGEdge())
                            {
                                wrapperFlag = false;
                            }
                                // if this is a return edge
                            else if(edge->isRetDirectVFGEdge())
                            {
                                reachFunExit = true;
                                csIdSet.insert(svfg->getCallSite(SVFUtil::cast<RetDirSVFGEdge>(edge)->getCallSiteId()));
                            }
                                // if this is an intra edge
                            else
                            {
                                const SVFGNode* succ = edge->getDstNode();
                                if (SVFUtil::isa<CopySVFGNode>(succ) || SVFUtil::isa<GepSVFGNode>(succ)
                                    || SVFUtil::isa<PHISVFGNode>(succ) || SVFUtil::isa<FormalRetSVFGNode>(succ)
                                    || SVFUtil::isa<ActualRetSVFGNode>(succ))
                                {
                                    worklist2.push(succ);
                                }
                                else
                                {
                                    wrapperFlag = false;
                                }
                            }
                        }
                    }
                    if(reachFunExit)
                        wrapperFlag = true;
                    else
                        wrapperFlag = false;


                    // if this node is in an allocation wrapper, find all its call nodes
                    if (wrapperFlag)
                    {
                        for (auto it3 = csIdSet.begin(), eit3 =
                                csIdSet.end(); it3 != eit3; ++it3)
                        {
                            worklist.push(*it3);
                        }
                    }
                        // otherwise, this is the source we are interested
                    else
                    {
                        // exclude sources in dead functions
                        if (SVFUtil::isPtrInDeadFunction(cs->getCallSite()) == false)
                        {
                            //errs() << *node << "\n";
                            if (node->getNodeKind() == VFGNode::VFGNodeK::Addr) {

                                //if there is non-constant argument
                                bool AllConstantOperand = true;
                                auto allocCS = cs->getCallSite();
                                for (auto argIt=allocCS->op_begin(), endIt=allocCS->op_end(); argIt != endIt; ++argIt){
                                    if (!isa<Constant>(argIt->get())) {
                                        AllConstantOperand = false;
                                    }
                                }

                                if (!AllConstantOperand)
                                    SVFGAllocationNodeSet.insert(node);
                            }
                        }
                    }
                }
            }
        }
    }

    LLVM_DEBUG(dbgs() << "Checking the whether the allocation sites are bounded or not...\n");

    auto t2 = high_resolution_clock::now();

    auto TaintedNodes = AM.getResult<DPPRule1G>(M);

    auto t3 = high_resolution_clock::now();

    /// write some logs to file
    string dppLog = "#################### RULE 9 #########################\n";

    for (auto Node: SVFGAllocationNodeSet) {
        /// ignore allocation sites that are not tainted
        if (TaintedNodes.TaintedSVFObjNodes.find(Node->getId()) == TaintedNodes.TaintedSVFObjNodes.end())
            continue;

        /// getting all the reachable nodes reachable from either the svf node or its param nodes
        set<uint32_t> reachableNodes = getReachableNodes(Node, pag, svfg);

        /// getting all the cfg paths ended up with the svf node
        auto cmpPaths = getCmpPaths(Node->getICFGNode());
        auto totalPaths = cmpPaths.size();
        auto totalRelevantCmpPaths = 0; ///paths with data dependent cmp instructions

        for (auto path: cmpPaths) {
            for (auto node: path) {
                if (node->getNodeKind() == ICFGNode::IntraBlock) {
                    if (auto *IBN = SVFUtil::dyn_cast<IntraBlockNode>(node)) {
                        if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(IBN->getInst())) {
                            if (I->getOpcode() == Instruction::ICmp || I->getOpcode() == Instruction::FCmp) {
                                if (checkCommonAncestor(node->getVFGNodes(), reachableNodes)) {
                                    if (! isa<Constant>(I->getOperand(1))) {
                                        //errs() << *I << "\n";
                                        totalRelevantCmpPaths++;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        //errs() << "Number of paths = " << totalPaths << ", relevant cmp paths = " << totalRelevantCmpPaths << "\n";
        if (totalRelevantCmpPaths <= 0) {
            dppLog += Node->toString() + "\n";
            dppLog += "--------------------------------------------------------------\n";

            if (auto *AVF = SVFUtil::dyn_cast<AddrVFGNode>(Node)) {
                if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(AVF->getInst())) {
                    Result.PrioritizedPtrMap.try_emplace(I, 1);
                }
            }
        }
    }

    auto t4 = high_resolution_clock::now();

    dppLog += "##################################################\n\n\n";
    if (DPP::isLogIndividualRule())
        DPP::writeDPPLogsToFile(dppLog);

    runtime_ms = (t2 - t1) + (t4 - t3);

    std::cout.precision(2);
    std::cout << "Rule9 done...time taken = " << std::fixed << runtime_ms.count()/1000 << "\n";
    
    return Result;
}

raw_ostream &DPPRule9GResult::print(raw_ostream &OS) const {
    OS << "Prioritized data objects by Rule 9:\n";
    for (auto Ptr : PrioritizedPtrMap) {
        auto *const I = Ptr.getFirst();
        auto score = Ptr.getSecond();
        OS << *I << ", score = " << score << "\n";
    }
    return OS;
}

