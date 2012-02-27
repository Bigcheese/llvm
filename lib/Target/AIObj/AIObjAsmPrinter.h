//===-- AIObjAsmPrinter.h - Print machine code to a AIObj file --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// AIObj Assembly printer class.
//
//===----------------------------------------------------------------------===//

#ifndef AIOBJASMPRINTER_H
#define AIOBJASMPRINTER_H

#include "AIObj.h"
#include "AIObjTargetMachine.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class MCOperand;

class LLVM_LIBRARY_VISIBILITY AIObjAsmPrinter : public AsmPrinter {
public:
  explicit AIObjAsmPrinter(TargetMachine &TM, MCStreamer &Streamer)
    : AsmPrinter(TM, Streamer) {}

  const char *getPassName() const { return "AIObj Assembly Printer"; }

  bool doFinalization(Module &M);

  virtual void EmitStartOfAsmFile(Module &M);
  virtual void EmitFunctionBodyStart();
  virtual void EmitFunctionBodyEnd();
  virtual void EmitFunctionEntryLabel();
  virtual void EmitInstruction(const MachineInstr *MI);

  unsigned GetOrCreateSourceID(StringRef FileName,
                               StringRef DirName);

  MCOperand GetSymbolRef(const MachineOperand &MO, const MCSymbol *Symbol);
  MCOperand lowerOperand(const MachineOperand &MO);

private:
  void EmitVariableDeclaration(const GlobalVariable *gv);
  void EmitFunctionDeclaration(const Function* func);

  StringMap<unsigned> SourceIdMap;
}; // class AIObjAsmPrinter
} // namespace llvm

#endif

