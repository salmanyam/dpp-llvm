//
// Created by salman on 7/14/21.
//

#ifndef DPP_LLVM_AARCH64DPPPASSCOMMON_H
#define DPP_LLVM_AARCH64DPPPASSCOMMON_H


#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/DPP/DPPUtils.h"

using namespace llvm;
using namespace llvm::DPP;

namespace llvm {
namespace DPP {

class AArch64DppPassCommon {
protected:
    inline void initRunOn(MachineFunction &MF);

    inline bool hasNoPartsAttribute(MachineFunction &MF);

    inline void lowerPartsIntrinsic(MachineFunction &MF, MachineBasicBlock &MBB, MachineInstr &MI, const MCInstrDesc &InstrDesc);
    inline void insertMovInstr(MachineBasicBlock &MBB, MachineInstr *MI, unsigned dstReg, unsigned srcReg);
    inline void replacePartsIntrinsic(MachineFunction &MF, MachineBasicBlock &MBB, MachineInstr &MI, const MCInstrDesc &InstrDesc);
    inline void replacePartsXPACDIntrinsic(MachineFunction &MF, MachineBasicBlock &MBB, MachineInstr &MI);

    const TargetMachine *TM = nullptr;
    const AArch64Subtarget *STI = nullptr;
    const AArch64InstrInfo *TII = nullptr;
    const AArch64RegisterInfo *TRI = nullptr;
public:
    static inline void insertPACInstr(MachineBasicBlock &MBB, MachineInstr *MI, unsigned dstReg,
                                      unsigned modReg, const MCInstrDesc &InstrDesc);
};

}
}

inline void AArch64DppPassCommon::initRunOn(MachineFunction &MF) {
    TM = &MF.getTarget();;
    STI = &MF.getSubtarget<AArch64Subtarget>();
    TII = STI->getInstrInfo();
    TRI = STI->getRegisterInfo();
}

inline bool AArch64DppPassCommon::hasNoPartsAttribute(MachineFunction &MF) {
    return MF.getFunction().getFnAttribute("no-parts").getValueAsString() == "true";
}

inline void AArch64DppPassCommon::lowerPartsIntrinsic(MachineFunction &MF,
                                                      MachineBasicBlock &MBB,
                                                      MachineInstr &MI,
                                                      const MCInstrDesc &InstrDesc) {
    const unsigned mod = MI.getOperand(2).getReg();
    const unsigned dst = MI.getOperand(0).getReg();

    insertPACInstr(MBB, &MI, dst, mod, InstrDesc);
}

inline void AArch64DppPassCommon::replacePartsIntrinsic(MachineFunction &MF,
                                                        MachineBasicBlock &MBB,
                                                        MachineInstr &MI,
                                                        const MCInstrDesc &InstrDesc) {
    lowerPartsIntrinsic(MF, MBB, MI, InstrDesc);
    MI.removeFromParent();
}

inline void AArch64DppPassCommon::insertPACInstr(MachineBasicBlock &MBB,
                                                 MachineInstr *MI,
                                                 unsigned dstReg,
                                                 unsigned modReg,
                                                 const MCInstrDesc &InstrDesc) {
    if (MI != nullptr)
        BuildMI(MBB, MI, MI->getDebugLoc(), InstrDesc, dstReg)
                .addUse(modReg);
    else
        BuildMI(&MBB, DebugLoc(), InstrDesc, dstReg)
                .addUse(modReg);
}

inline void AArch64DppPassCommon::insertMovInstr(MachineBasicBlock &MBB,
                                                 MachineInstr *MI,
                                                 unsigned dstReg,
                                                 unsigned srcReg) {
    BuildMI(MBB, MI, MI->getDebugLoc(), TII->get(AArch64::ORRXrs), dstReg)
            .addUse(AArch64::XZR)
            .addUse(srcReg)
            .addImm(0);
}

inline void AArch64DppPassCommon::replacePartsXPACDIntrinsic(MachineFunction &MF,
                                                             MachineBasicBlock &MBB,
                                                             MachineInstr &MI) {
    const unsigned dst = MI.getOperand(0).getReg();
    BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(AArch64::XPACD), dst);
    MI.removeFromParent();
}

#endif //DPP_LLVM_AARCH64DPPPASSCOMMON_H
