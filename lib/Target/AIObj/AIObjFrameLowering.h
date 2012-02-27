//===-- AIObjFrameLowering.h - Define frame lowering for AIObj --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef AIOBJ_FRAMEINFO_H
#define AIOBJ_FRAMEINFO_H

#include "AIObj.h"
#include "AIObjSubtarget.h"
#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {
  class AIObjSubtarget;

class AIObjFrameLowering : public TargetFrameLowering {
  const AIObjSubtarget &STI;
public:
  explicit AIObjFrameLowering(const AIObjSubtarget &sti)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, 8, 0), STI(sti) {
  }

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF) const;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const;

  bool hasFP(const MachineFunction &MF) const { return false; }
};

} // End llvm namespace

#endif
