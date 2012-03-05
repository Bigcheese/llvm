//===-- AIObjRegisterInfo.h - AIObj Register Information Impl ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the AIObj implementation of the MRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef AIOBJ_REGISTER_INFO_H
#define AIOBJ_REGISTER_INFO_H

#include "llvm/Support/ErrorHandling.h"
#include "llvm/ADT/BitVector.h"

#define GET_REGINFO_HEADER
#include "AIObjGenRegisterInfo.inc"

namespace llvm {
class AIObjTargetMachine;
class MachineFunction;

struct AIObjRegisterInfo : public AIObjGenRegisterInfo {
private:
  const TargetInstrInfo &TII;

public:
  AIObjRegisterInfo(AIObjTargetMachine &TM,
                  const TargetInstrInfo &tii);

  virtual const uint16_t
    *getCalleeSavedRegs(const MachineFunction *MF = 0) const {
    static const uint16_t CalleeSavedRegs[] = { 0 };
    return CalleeSavedRegs; // save nothing
  }

  virtual BitVector getReservedRegs(const MachineFunction &MF) const {
    BitVector Reserved(getNumRegs());
    return Reserved; // reserve no regs
  }

  virtual void eliminateFrameIndex(MachineBasicBlock::iterator II,
                                   int SPAdj,
                                   RegScavenger *RS = NULL) const;

  virtual unsigned getFrameRegister(const MachineFunction &MF) const {
    llvm_unreachable("AIObj does not have a frame register");
  }
}; // struct AIObjRegisterInfo
} // namespace llvm

#endif // AIOBJ_REGISTER_INFO_H
