//
// Created by salman on 7/26/21.
//
#include <chrono>
#include <iostream>

#include "llvm/DPP/SVFInitPass.h"

#include "Graphs/SVFG.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/PAGBuilder.h"
#include "WPA/Andersen.h"
//#include "WPA/Steensgaard.h"
//#include "WPA/FlowSensitive.h"
//#include "WPA/VersionedFlowSensitive.h"

#define DEBUG_TYPE "SVFInitPass"

using namespace llvm;
using namespace llvm::DPP;
using namespace SVF;


[[maybe_unused]] const char SVFInitPass::RuleName[] = "SVFInitPass";
AnalysisKey SVFInitPass::Key;


SVFInitPass::Result SVFInitPass::run(Module &M, AnalysisManager<Module> &AM) {
    Result Result {};

    using std::chrono::high_resolution_clock;
    using std::chrono::duration;
    duration<double, std::milli> runtime_ms;

    auto t1 = high_resolution_clock::now();

    SVFModule* svfModule = LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(M);
    //svfModule->buildSymbolTableInfo();

    /// Build Program Assignment Graph (PAG)
    PAGBuilder builder;
    PAG* pag = builder.build(svfModule);

    /// Create Andersen's pointer analysis
    Andersen* ander = AndersenWaveDiff::createAndersenWaveDiff(pag);
    //Steensgaard* ander = Steensgaard::createSteensgaard(pag);
    //FlowSensitive* ander = FlowSensitive::createFSWPA(pag);
    //VersionedFlowSensitive * ander = VersionedFlowSensitive::createVFSWPA(pag);

    /// Call Graph
    PTACallGraph* callgraph = ander->getPTACallGraph();

    /// Sparse value-flow graph (SVFG)
    SVFGBuilder svfBuilder;
    SVFG* svfg = svfBuilder.buildFullSVFGWithoutOPT(ander);

    auto t2 = high_resolution_clock::now();

    runtime_ms = t2 - t1;
    std::cout.precision(2);
    std::cout << "SVF Initialization Done, time taken = " << std::fixed << runtime_ms.count()/1000 << "\n";

    Result.SVFParams.pag = pag;
    Result.SVFParams.CallGraph = callgraph;
    Result.SVFParams.svfg = svfg;

    return Result;
}
