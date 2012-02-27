//===-- AIObjRegisterInfo.cpp - AIObj Register Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the AIObj implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "AIObj.h"
#include "AIObjRegisterInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define GET_REGINFO_TARGET_DESC
#include "AIObjGenRegisterInfo.inc"

using namespace llvm;

AIObjRegisterInfo::AIObjRegisterInfo(AIObjTargetMachine &TM,
                                 const TargetInstrInfo &tii)
  // AIObj does not have a return address register.
  : AIObjGenRegisterInfo(0), TII(tii) {
}

void AIObjRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator /*II*/,
                                          int /*SPAdj*/,
                                          RegScavenger * /*RS*/) const {
  llvm_unreachable("FrameIndex should have been previously eliminated!");
}
