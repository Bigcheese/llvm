//===-- AIObjInstrInfo.cpp - AIObj Instruction Information ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the AIObj implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "AIObjInstrInfo.h"
#include "AIObj.h"
#include "AIObjMachineFunctionInfo.h"
#include "AIObjSubtarget.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#define GET_INSTRINFO_CTOR
#include "AIObjGenInstrInfo.inc"

using namespace llvm;

AIObjInstrInfo::AIObjInstrInfo(AIObjTargetMachine &TarM)
  : AIObjGenInstrInfo()
  , RI(TarM, *this)
  , TM(TarM) {
}

/// isLoadFromStackSlot - If the specified machine instruction is a direct
/// load from a stack slot, return the virtual or physical register number of
/// the destination along with the FrameIndex of the loaded stack slot.  If
/// not, return 0.  This predicate must return 0 if the instruction has
/// any side effects other than loading from the stack slot.
unsigned AIObjInstrInfo::isLoadFromStackSlot(const MachineInstr *MI,
                                             int &FrameIndex) const {
  return 0;
}

/// isStoreToStackSlot - If the specified machine instruction is a direct
/// store to a stack slot, return the virtual or physical register number of
/// the source reg along with the FrameIndex of the loaded stack slot.  If
/// not, return 0.  This predicate must return 0 if the instruction has
/// any side effects other than storing to the stack slot.
unsigned AIObjInstrInfo::isStoreToStackSlot(const MachineInstr *MI,
                                            int &FrameIndex) const {
  return 0;
}

MachineInstr *
AIObjInstrInfo::emitFrameIndexDebugValue(MachineFunction &MF,
                                         int FrameIx,
                                         uint64_t Offset,
                                         const MDNode *MDPtr,
                                         DebugLoc dl) const {
  return 0;
}


bool AIObjInstrInfo::AnalyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const {
  return true;
}

unsigned
AIObjInstrInfo::InsertBranch(MachineBasicBlock &MBB,MachineBasicBlock *TBB,
                             MachineBasicBlock *FBB,
                             const SmallVectorImpl<MachineOperand> &Cond,
                             DebugLoc DL) const {
  llvm_unreachable("Unimplemented!");
}

unsigned AIObjInstrInfo::RemoveBranch(MachineBasicBlock &MBB) const
{
  llvm_unreachable("Unimplemented!");
}

void AIObjInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I, DebugLoc DL,
                                 unsigned DestReg, unsigned SrcReg,
                                 bool KillSrc) const {
  llvm_unreachable("Unimplemented!");
}

void AIObjInstrInfo::
storeRegToStackSlot(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                    unsigned SrcReg, bool isKill, int FI,
                    const TargetRegisterClass *RC,
                    const TargetRegisterInfo *TRI) const {
  llvm_unreachable("Unimplemented!");
}

void AIObjInstrInfo::
loadRegFromStackSlot(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                     unsigned DestReg, int FI,
                     const TargetRegisterClass *RC,
                     const TargetRegisterInfo *TRI) const {
  llvm_unreachable("Unimplemented!");
}

unsigned AIObjInstrInfo::getGlobalBaseReg(MachineFunction *MF) const
{
  llvm_unreachable("Unimplemented!");
}
