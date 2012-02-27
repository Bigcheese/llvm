//===-- AIObjTargetMachine.h - Define TargetMachine for AIObj ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the AIObj specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef AIOBJTARGETMACHINE_H
#define AIOBJTARGETMACHINE_H

#include "AIObjInstrInfo.h"
#include "AIObjISelLowering.h"
#include "AIObjFrameLowering.h"
#include "AIObjSelectionDAGInfo.h"
#include "AIObjSubtarget.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {

class AIObjTargetMachine : public LLVMTargetMachine {
  AIObjSubtarget Subtarget;
  const TargetData DataLayout; // Calculates type size & alignment
  AIObjTargetLowering TLInfo;
  AIObjSelectionDAGInfo TSInfo;
  AIObjInstrInfo InstrInfo;
  AIObjFrameLowering FrameLowering;
public:
  AIObjTargetMachine( const Target &T
                    , StringRef TT
                    , StringRef CPU
                    , StringRef FS
                    , const TargetOptions &Options
                    , Reloc::Model RM
                    , CodeModel::Model CM
                    , CodeGenOpt::Level OL);

  virtual const AIObjInstrInfo *getInstrInfo() const { return &InstrInfo; }
  virtual const TargetFrameLowering  *getFrameLowering() const {
    return &FrameLowering;
  }
  virtual const AIObjSubtarget *getSubtargetImpl() const{ return &Subtarget; }
  virtual const AIObjRegisterInfo *getRegisterInfo() const {
    return &InstrInfo.getRegisterInfo();
  }
  virtual const AIObjTargetLowering *getTargetLowering() const {
    return &TLInfo;
  }
  virtual const AIObjSelectionDAGInfo *getSelectionDAGInfo() const {
    return &TSInfo;
  }
  virtual const TargetData *getTargetData() const { return &DataLayout; }

  // Pass Pipeline Configuration
  virtual TargetPassConfig *createPassConfig(PassManagerBase &PM);
};

} // end namespace llvm

#endif
