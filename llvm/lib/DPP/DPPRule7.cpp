//
// Created by salman on 3/8/21.
//

#include "llvm/DPP/DPPRule7.h"
#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

void Rule7Global(PAG *pag, PTACallGraph* callgraph, SVFG *svfg);

using namespace llvm;
using namespace llvm::DPP;
using namespace std;
using namespace SVF;

// for manually inserting input reading functions
InputFunctionMap InputFunctions;
InputFunction IF_scanf = {"__isoc99_scanf", 1, false};
InputFunction IF_fscanf = {"__isoc99_fscanf", 2, false};
InputFunction IF_freaduntil = {"freaduntil", 1, false};

//todo: marking the return type as input dependent


// for tracking the SVFG nodes that are dependent on input
NodeSet InputSVFGNodes;
DenseMap<const VFGNode *, bool> SafeVFGs;

void Rule7Init() {
    // clear any SVFG nodes from the dataset
    InputSVFGNodes.clear();

    // clear the VFG nodes from dataset
    SafeVFGs.clear();

    // Insert the input functions
    InputFunctions.try_emplace(IF_scanf.name, &IF_scanf);
    InputFunctions.try_emplace(IF_fscanf.name, &IF_fscanf);
    InputFunctions.try_emplace(IF_freaduntil.name, &IF_freaduntil);
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
            assert(SVFG->getInst()->getNumOperands() > 1);
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

void lookupAllocationSite(const VFGNode* vNode) {

    FIFOWorkList<const VFGNode*> worklist;
    Set<const VFGNode*> visited;
    worklist.push(vNode);

    /// Traverse along VFG
    while (!worklist.empty())
    {
        const VFGNode* vNode = worklist.pop();
        errs() << *vNode << "\n";

        // if an allocation site is found, then propagate the input
        // dependency to all successor nodes
        if (vNode->getNodeKind() == VFGNode::VFGNodeK::Addr) {
            errs() << "Found AddrVFGNode = " << *vNode << "\n";
            errs() << "****************PROPAGATION START******************\n";
            markInputNodesOnSVFG(vNode);
            errs() << "****************PROPAGATION END******************\n";
        }

        for (VFGNode::const_iterator it = vNode->InEdgeBegin(), eit =
                vNode->InEdgeEnd(); it != eit; ++it) {
            VFGEdge *edge = *it;

            // skip the indirect relationship for finding allocation site for input dependency
            if (edge->isIndirectVFGEdge())
                continue;

            VFGNode *predNode = edge->getSrcNode();

            if (visited.find(predNode) == visited.end()) {
                visited.insert(predNode);
                worklist.push(predNode);
            }
        }
    }
}

const VFGNode* getVFGNodeFromValue(PAG *pag, SVFG *svfg, const Value *val) {
    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(val));
    const VFGNode* vNode = svfg->getDefSVFGNode(pNode);
    return vNode;
}

void Rule7Global(PAG *pag, PTACallGraph* callgraph, SVFG *svfg) {
    // Rule 7: data pointers dependent on external source
    // External sources in this case are function calls

    // Initialize the manually inserted input reading functions
    Rule7Init();

    ICFG *icfg = pag->getICFG();

    // Get all call sites, filter input reading functions, and
    // mark the nodes dependent on input reading functions
    for(const CallBlockNode *CS: pag->getCallSiteSet()) {
        // get the names of the calle functions to filter input functions
        PTACallGraph::FunctionSet callees;
        callgraph->getCallees(CS,callees);
        for(PTACallGraph::FunctionSet::const_iterator cit = callees.begin(), ecit = callees.end(); cit!=ecit; cit++) {
            const SVFFunction *func = *cit;
            errs() << func->getName() << "\n";

            if (isInputReadingFunction(func->getName())) {
                errs() << func->getName() << " " << getInputArgStart(func->getName()) << "\n";
                unsigned int paramIndex = 0;
                for (auto P: CS->getActualParms()) {
                    // skip until the meaning params come
                    if (paramIndex < getInputArgStart(func->getName())) {
                        paramIndex++;
                        continue;
                    }

                    errs() << *P << "\n";
                    //errs() << "SVFG Node ID = " << *svfg->getDefSVFGNode(P) << "\n";

                    //lookup the allocation or definition sites and propagate the info
                    lookupAllocationSite(svfg->getDefSVFGNode(P));

                    //errs() << "****************TRAVERSE START******************\n";
                    //PAGNode* pNode = pag->getPAGNode(pag->getValueNode(Arg->get()));
                    //const VFGNode* vNode = svfg->getDefSVFGNode(pNode);
                    //errs() << "SVFG Node = " << *vNode << "\n";
                    //markInputNodesOnSVFG(svfg, P->getValue());
                    //errs() << "****************TRAVERSE END******************\n";

                    paramIndex++;

                }
                errs() << "----------------------------------------------------\n";
            }
        }
        // errs() << "--------------------\n";

        //const CallInst *CI = dyn_cast<CallInst>(CS->getCallSite());
        //errs() << "Called function = " << CI->getCalledFunction()->getName() << "\n";

        /*if (CI->getCalledFunction()->getName() == "malloc"){
            for (auto Arg = CI->arg_begin(); Arg != CI->arg_end(); ++Arg) {
                errs() << *Arg->get() << "\n++++++++++++++\n";
                errs() << "SVFG Node ID = " << svfg->getDefSVFGNode(pag->getPAGNode(pag->getValueNode(Arg->get())))->getId() << "\n";
            }
        }*/

        //if (CI->getCalledFunction()->getName() != "__isoc99_scanf")
        //continue;

        /*for (auto Arg = CI->arg_begin(); Arg != CI->arg_end(); ++Arg) {
            errs() << *Arg->get() << "\n++++++++++++++\n";
            errs() << "****************TRAVERSE START******************\n";
            PAGNode* pNode = pag->getPAGNode(pag->getValueNode(Arg->get()));
            const VFGNode* vNode = svfg->getDefSVFGNode(pNode);
            errs() << "SVFG Node = " << *vNode << "\n";
            traverseOnVFG(svfg, Arg->get());
            errs() << "****************TRAVERSE END******************\n";
        }*/


        /*if (CS->getCallSite()->getName() == "malloc") {
            for (auto P: CS->getActualParms()) {
                errs() << *P << "\n";
            }
            errs() << "++++++++++++++++++\n";
        }*/
    }

    //errs() << "==================malloc========================\n";

    // for each address taken memory allocation, propagate the input flow
    for(auto SV = svfg->begin(); SV != svfg->end(); ++SV) {
        VFGNode *V = SV->second;
        if (AddrVFGNode *AVFG = SVFUtil::dyn_cast<AddrVFGNode>(V)) {
            //errs() << *AVFG << "\n";
            //errs() << *AVFG->getICFGNode() << "\n";
            // to discard the function and global constant type nodes
            if (AVFG->getICFGNode()->getId() == 0) //todo: revisit here regarding ID 0
                continue;

            // All the leftover nodes are instruction type
            if (const Instruction *I = SVFUtil::dyn_cast<Instruction>(AVFG->getInst())) {
                //skip the alloca instructions
                if (I->getOpcode() == Instruction::Alloca)
                    continue;

                //errs() << *I << "\n";
                //errs() << I->getNumOperands() << "\n";

                // loop through all the operands and propagate the dependency if any
                // operands depend on user input
                for (auto Op = I->op_begin(); Op != I->op_end(); ++Op) {
                    //errs() << *Op->get() << "\n";
                    const VFGNode* vNode = getVFGNodeFromValue(pag, svfg, Op->get());
                    //errs() << *vNode << "\n";

                    // if the operand depends on user input, propagate the depedency
                    if (InputSVFGNodes.find(vNode->getId()) != InputSVFGNodes.end()) {
                        vNode = getVFGNodeFromValue(pag, svfg, I);
                        //errs() << *vNode << "\n";
                        errs() << "****************OPDEP PROPAGATION START******************\n";
                        markInputNodesOnSVFG(vNode);
                        errs() << "****************OPDEP PROPAGATION END******************\n";
                    }
                }
            }
        }
    }

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
                    //SafeVFGs.try_emplace(vNode, isFoundCondition);
            }
        }
    }

    for (auto VFG : SafeVFGs)
        errs() << *VFG.getFirst() << ", found condition = " << VFG.getSecond() << "\n";


    // for each address taken object, determine the sensitive data pointers
    for(auto SV = svfg->begin(); SV != svfg->end(); ++SV) {
        VFGNode *V = SV->second;
        if (AddrVFGNode *AVFG = SVFUtil::dyn_cast<AddrVFGNode>(V)) {
            //errs() << AVFG->getPAGEdge()->getValue()->getName() << "\n";
            auto Ty = AVFG->getPAGEdge()->getValue()->getType();
            if (Ty && Ty->isPointerTy()) {
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

    // input SVFG nodes
    for (auto ID: InputSVFGNodes){
        //errs() << ID << "\n";
    }


}


AnalysisKey DPPRule7G::Key;
[[maybe_unused]] const char DPPRule7G::RuleName[] = "DPPRule7G";

DPPRule7G::Result DPPRule7G::run(Module &M, AnalysisManager<Module> &AM) {
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

  Rule7Global(PAG, Callgraph, SVFG);

  return Result;
}

raw_ostream &DPPRule7GResult::print(raw_ostream &OS) const {
  OS << "not implemented\n";
  return OS;
}
