//===-- AIObjFrameLowering.cpp - AIObj Frame Information ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the AIObj implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "AIObjFrameLowering.h"
#include "AIObjInstrInfo.h"
#include "AIObjMachineFunctionInfo.h"
#include "llvm/Function.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

void AIObjFrameLowering::emitPrologue(MachineFunction &MF) const {
}

void AIObjFrameLowering::emitEpilogue(MachineFunction &MF,
                                  MachineBasicBlock &MBB) const {
}
