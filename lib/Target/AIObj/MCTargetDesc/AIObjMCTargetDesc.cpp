//===-- AIObjMCTargetDesc.cpp - AIObj Target Descriptions -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides AIObj specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "AIObjMCTargetDesc.h"
#include "AIObjMCAsmInfo.h"
#include "InstPrinter/AIObjInstPrinter.h"
#include "llvm/MC/MCCodeGenInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"

#define GET_INSTRINFO_MC_DESC
#include "AIObjGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "AIObjGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "AIObjGenRegisterInfo.inc"

using namespace llvm;

static MCInstrInfo *createAIObjMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitAIObjMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createAIObjMCRegisterInfo(StringRef TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitAIObjMCRegisterInfo(X, AIObj::DUMMY_REG);
  return X;
}

static MCSubtargetInfo *createAIObjMCSubtargetInfo(StringRef TT, StringRef CPU,
                                                   StringRef FS) {
  MCSubtargetInfo *X = new MCSubtargetInfo();
  InitAIObjMCSubtargetInfo(X, TT, CPU, FS);
  return X;
}

static MCCodeGenInfo *createAIObjMCCodeGenInfo(StringRef TT, Reloc::Model RM,
                                               CodeModel::Model CM,
                                               CodeGenOpt::Level OL) {
  MCCodeGenInfo *X = new MCCodeGenInfo();
  X->InitMCCodeGenInfo(RM, CM, OL);
  return X;
}

static MCInstPrinter *createAIObjMCInstPrinter(const Target &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCRegisterInfo &MRI,
                                             const MCSubtargetInfo &STI) {
  assert(SyntaxVariant == 0 && "We only have one syntax variant");
  return new AIObjInstPrinter(MAI, MRI, STI);
}

extern "C" void LLVMInitializeAIObjTargetMC() {
  // Register the MC asm info.
  RegisterMCAsmInfo<AIObjMCAsmInfo> X(TheAIObjTarget);

  // Register the MC codegen info.
  TargetRegistry::RegisterMCCodeGenInfo(TheAIObjTarget,
                                        createAIObjMCCodeGenInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(TheAIObjTarget, createAIObjMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(TheAIObjTarget, createAIObjMCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(TheAIObjTarget,
                                          createAIObjMCSubtargetInfo);

  TargetRegistry::RegisterMCInstPrinter(TheAIObjTarget,
                                        createAIObjMCInstPrinter);
}
