//
// Created by salman on 6/30/21.
//

#include <iostream>
// LLVM includes
#include "AArch64.h"
#include "AArch64Subtarget.h"
#include "AArch64RegisterInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
// DPP includes
//#include "llvm/PARTS/Parts.h"
//#include "llvm/PARTS/PartsEventCount.h"
#include "AArch64DPPPassCommon.h"
//#include "PartsUtils.h"

#define DEBUG_TYPE "AArch64DppDpiPass"

STATISTIC(StatDataStore, DEBUG_TYPE ": instrumented data stores");
STATISTIC(StatDataLoad, DEBUG_TYPE ": instrumented data loads");
STATISTIC(StatInsecureDataLoad, DEBUG_TYPE ": Insecure data loads");

using namespace llvm;
using namespace llvm::DPP;

namespace {

class AArch64DppDpiPass : public MachineFunctionPass, private AArch64DppPassCommon {
public:
    static char ID;
    AArch64DppDpiPass() : MachineFunctionPass(ID) {}
    StringRef getPassName() const override { return DEBUG_TYPE; }
    bool runOnMachineFunction(MachineFunction &MF) override;
    bool lowerDpiIntrinsics(MachineFunction& MF);
};

} //namespace


FunctionPass *llvm::createDppPassDpi() {
    return new AArch64DppDpiPass();
}

char AArch64DppDpiPass::ID = 0;

bool AArch64DppDpiPass::runOnMachineFunction(MachineFunction &MF) {
    bool modified = false;

    initRunOn(MF);

    //errs() << "Machine Function: " << MF.getName() << "\n";
    modified |= lowerDpiIntrinsics(MF);

    return modified;
}

bool AArch64DppDpiPass::lowerDpiIntrinsics(MachineFunction& MF) {
    bool modified = false;

    for (auto &MBB : MF) {
        for (auto MBBI = MBB.begin(), end = MBB.end(); MBBI != end; ) {
            auto &MI = *MBBI++;

            //errs() << "Machine Instruction: " << MI << "\n";

            switch(MI.getOpcode()) {
                default:
                    break;
                case AArch64::PARTS_PACDA:
                    replacePartsIntrinsic(MF, MBB, MI, TII->get(AArch64::PACDA));
                    ++StatDataStore;
                    modified = true;
                    break;
                case AArch64::PARTS_AUTDA:
                    replacePartsIntrinsic(MF, MBB, MI, TII->get(AArch64::AUTDA));
                    modified = true;
                    ++StatDataLoad;
                    break;
                case AArch64::PARTS_XPACD:
                    replacePartsXPACDIntrinsic(MF, MBB, MI);
                    modified = true;
                    ++StatInsecureDataLoad;
                    break;
            }
        }
    }

    return modified;
}