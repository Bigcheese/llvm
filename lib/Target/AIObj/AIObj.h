//===-- AIObj.h - Top-level interface for AIObj representation --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// AIObj back-end.
//
//===----------------------------------------------------------------------===//

#ifndef TARGET_AIOBJ_H
#define TARGET_AIOBJ_H

#include "MCTargetDesc/AIObjMCTargetDesc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
  class AIObjAsmPrinter;
  class FunctionPass;
  class AIObjTargetMachine;
  class formatted_raw_ostream;
  class FunctionPass;
  class MachineInstr;
  class MCInst;

  FunctionPass *createAIObjISelDag(AIObjTargetMachine &TM);
  FunctionPass *createAIObjRegisterAllocator();

  void LowerAIObjMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                      AIObjAsmPrinter &AP);
} // end namespace llvm
#endif
