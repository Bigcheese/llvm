//===-- AIObjAsmPrinter.cpp - AIObj LLVM assembly writer ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to AIObj assembly language.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "aiobj-asm-printer"

#include "AIObj.h"
#include "AIObjAsmPrinter.h"
#include "AIObjMachineFunctionInfo.h"
#include "AIObjRegisterInfo.h"
#include "AIObjTargetMachine.h"
#include "llvm/Argument.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/DebugInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Target/Mangler.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

bool AIObjAsmPrinter::doFinalization(Module &M) {
  // Allow the target to emit any magic that it wants at the end of the file,
  // after everything else has gone out.
  EmitEndOfAsmFile(M);

  delete Mang; Mang = 0;
  MMI = 0;

  OutStreamer.Finish();
  return false;
}

void AIObjAsmPrinter::EmitStartOfAsmFile(Module &M)
{
  // Print metadata directives.
  unsigned SizeofPointer = 8;
  unsigned SharedFactoryVersion = 69;
  unsigned NPCHVersion = 79;
  unsigned NASCVersion = 2;
  unsigned NPCEventHVersion = 2;
  SmallVector<Module::ModuleFlagEntry, 5> Flags;
  M.getModuleFlagsMetadata(Flags);
  for (SmallVectorImpl<Module::ModuleFlagEntry>::const_iterator
         i = Flags.begin(), e = Flags.end(); i != e; ++i) {
    if (i->Key->getString() == "SizeofPointer") {
      ConstantInt *CI = dyn_cast<ConstantInt>(i->Val);
      assert(CI  && "Must be constant int");
      SizeofPointer = CI->getSExtValue();
    } if (i->Key->getString() == "SharedFactoryVersion") {
      ConstantInt *CI = dyn_cast<ConstantInt>(i->Val);
      assert(CI  && "Must be constant int");
      SharedFactoryVersion = CI->getSExtValue();
    } if (i->Key->getString() == "NPCHVersion") {
      ConstantInt *CI = dyn_cast<ConstantInt>(i->Val);
      assert(CI  && "Must be constant int");
      NPCHVersion = CI->getSExtValue();
    } if (i->Key->getString() == "NASCVersion") {
      ConstantInt *CI = dyn_cast<ConstantInt>(i->Val);
      assert(CI  && "Must be constant int");
      NASCVersion = CI->getSExtValue();
    } if (i->Key->getString() == "NPCEventHVersion") {
      ConstantInt *CI = dyn_cast<ConstantInt>(i->Val);
      assert(CI  && "Must be constant int");
      NPCEventHVersion = CI->getSExtValue();
    }
  }

  OutStreamer.EmitRawText(Twine("SizeofPointer ") + Twine(SizeofPointer) + "\n"
    + "SharedFactoryVersion " + Twine(SharedFactoryVersion) + "\n"
    + "NPCHVersion " + Twine(NPCHVersion) + "\n"
    + "NASCVersion " + Twine(NASCVersion) + "\n"
    + "NPCEventHVersion " + Twine(NPCEventHVersion) + "\n"
    + "Debug 0\n");
  OutStreamer.AddBlankLine();
}

void AIObjAsmPrinter::EmitFunctionBodyStart() {
  // Calculate the size of the function in opcodes...
  unsigned Size = 0;
  for (MachineFunction::const_iterator i = MF->begin(), e = MF->end(); i != e;
                                       ++i) {
    Size += i->size();
  }

  OutStreamer.EmitRawText(Twine("handler 3 ") + Twine(Size));
}

void AIObjAsmPrinter::EmitFunctionBodyEnd() {
  OutStreamer.EmitRawText(Twine("handler_end"));
}

void AIObjAsmPrinter::EmitInstruction(const MachineInstr *MI) {
  // This hack has to be done here because MCInst's don't have access to the
  // GV.
  if (MI->getOpcode() == AIObj::PUSH_STRING) {
    StringRef Value;
    getConstantStringInfo(MI->getOperand(1).getGlobal(), Value);
    OutStreamer.EmitRawText(Twine(Mang->getSymbol(MI->getOperand(1).getGlobal())->getName())
                            + Twine(" \"") + Value + Twine('"'));
  }
  MCInst TmpInst;
  LowerAIObjMachineInstrToMCInst(MI, TmpInst, *this);
  OutStreamer.EmitInstruction(TmpInst);
}

void AIObjAsmPrinter::EmitVariableDeclaration(const GlobalVariable *gv) {
  // Check to see if this is a special global used by LLVM, if so, emit it.
  if (EmitSpecialLLVMGlobal(gv))
    return;
}

void AIObjAsmPrinter::EmitFunctionEntryLabel() {
  // AIObj doesn't have function entry labels.
}

void AIObjAsmPrinter::EmitFunctionDeclaration(const Function* func) {
  // AIObj doesn't have function declarations.
}

unsigned AIObjAsmPrinter::GetOrCreateSourceID(StringRef FileName,
                                            StringRef DirName) {
  // If FE did not provide a file name, then assume stdin.
  if (FileName.empty())
    return GetOrCreateSourceID("<stdin>", StringRef());

  // MCStream expects full path name as filename.
  if (!DirName.empty() && !sys::path::is_absolute(FileName)) {
    SmallString<128> FullPathName = DirName;
    sys::path::append(FullPathName, FileName);
    // Here FullPathName will be copied into StringMap by GetOrCreateSourceID.
    return GetOrCreateSourceID(StringRef(FullPathName), StringRef());
  }

  StringMapEntry<unsigned> &Entry = SourceIdMap.GetOrCreateValue(FileName);
  if (Entry.getValue())
    return Entry.getValue();

  unsigned SrcId = SourceIdMap.size();
  Entry.setValue(SrcId);

  return SrcId;
}

MCOperand AIObjAsmPrinter::GetSymbolRef(const MachineOperand &MO,
                                        const MCSymbol *Symbol) {
  const MCExpr *Expr;
  Expr = MCSymbolRefExpr::Create(Symbol, MCSymbolRefExpr::VK_None, OutContext);
  return MCOperand::CreateExpr(Expr);
}

MCOperand AIObjAsmPrinter::lowerOperand(const MachineOperand &MO) {
  MCOperand MCOp;
  const AIObjMachineFunctionInfo *MFI = MF->getInfo<AIObjMachineFunctionInfo>();
  unsigned EncodedReg;
  switch (MO.getType()) {
  default:
    llvm_unreachable("Unknown operand type");
  case MachineOperand::MO_Register:
    EncodedReg = 0;
    MCOp = MCOperand::CreateReg(EncodedReg);
    break;
  case MachineOperand::MO_Immediate:
    MCOp = MCOperand::CreateImm(MO.getImm());
    break;
  case MachineOperand::MO_MachineBasicBlock:
    MCOp = MCOperand::CreateExpr(MCSymbolRefExpr::Create(
                                 MO.getMBB()->getSymbol(), OutContext));
    break;
  case MachineOperand::MO_GlobalAddress:
    MCOp = GetSymbolRef(MO, Mang->getSymbol(MO.getGlobal()));
    break;
  case MachineOperand::MO_ExternalSymbol:
    MCOp = GetSymbolRef(MO, GetExternalSymbolSymbol(MO.getSymbolName()));
    break;
  case MachineOperand::MO_FPImmediate:
    APFloat Val = MO.getFPImm()->getValueAPF();
    bool ignored;
    Val.convert(APFloat::IEEEdouble, APFloat::rmTowardZero, &ignored);
    MCOp = MCOperand::CreateFPImm(Val.convertToDouble());
    break;
  }

  return MCOp;
}

void llvm::LowerAIObjMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                          AIObjAsmPrinter &AP) {
  OutMI.setOpcode(MI->getOpcode());
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    MCOperand MCOp;
    OutMI.addOperand(AP.lowerOperand(MO));
  }
}

// Force static initialization.
extern "C" void LLVMInitializeAIObjAsmPrinter() {
  RegisterAsmPrinter<AIObjAsmPrinter> X(TheAIObjTarget);
}
