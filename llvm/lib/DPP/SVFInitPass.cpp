//
// Created by salman on 7/26/21.
//

#include "llvm/DPP/SVFInitPass.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"

#define DEBUG_TYPE "SVFInitPass"

using namespace llvm;
using namespace llvm::DPP;
using namespace SVF;


[[maybe_unused]] const char SVFInitPass::RuleName[] = "SVFInitPass";
AnalysisKey SVFInitPass::Key;


SVFInitPass::Result SVFInitPass::run(Module &M, AnalysisManager<Module> &AM) {
    Result Result {};

    SVFModule* svfModule = LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(M);
    //svfModule->buildSymbolTableInfo();

    /// Build Program Assignment Graph (PAG)
    PAGBuilder builder;
    PAG* pag = builder.build(svfModule);

    /// Create Andersen's pointer analysis
    Andersen* ander = AndersenWaveDiff::createAndersenWaveDiff(pag);

    /// Call Graph
    PTACallGraph* callgraph = ander->getPTACallGraph();

    /// Sparse value-flow graph (SVFG)
    SVFGBuilder svfBuilder;
    SVFG* svfg = svfBuilder.buildFullSVFGWithoutOPT(ander);

    errs() << "SVF Initialization Done\n";

    Result.SVFParams.pag = pag;
    Result.SVFParams.CallGraph = callgraph;
    Result.SVFParams.svfg = svfg;

    return Result;
}
