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

    SVFModule *SVFModule = LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(M);

    PAGBuilder Builder;
    PAG *pag = Builder.build(SVFModule);

    Andersen *Andersen = AndersenWaveDiff::createAndersenWaveDiff(pag);

    PTACallGraph *CallGraph = Andersen->getPTACallGraph();

    SVFGBuilder SVFGBuilder;
    SVFG *svfg = SVFGBuilder.buildFullSVFGWithoutOPT(Andersen);

    errs() << "SVF Initialization Done\n";

    Result.SVFParams.pag = pag;
    Result.SVFParams.CallGraph = CallGraph;
    Result.SVFParams.svfg = svfg;

    return Result;
}
