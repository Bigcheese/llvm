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
  // XXX Temproarily remove global variables so that doFinalization() will not
  // emit them again (global variables are emitted at beginning).

  Module::GlobalListType &global_list = M.getGlobalList();
  int i, n = global_list.size();
  GlobalVariable **gv_array = new GlobalVariable* [n];

  // first, back-up GlobalVariable in gv_array
  i = 0;
  for (Module::global_iterator I = global_list.begin(), E = global_list.end();
       I != E; ++I)
    gv_array[i++] = &*I;

  // second, empty global_list
  while (!global_list.empty())
    global_list.remove(global_list.begin());

  // call doFinalization
  bool ret = AsmPrinter::doFinalization(M);

  // now we restore global variables
  for (i = 0; i < n; i ++)
    global_list.insert(global_list.end(), gv_array[i]);

  delete[] gv_array;
  return ret;
}

void AIObjAsmPrinter::EmitStartOfAsmFile(Module &M)
{
  const AIObjSubtarget& ST = TM.getSubtarget<AIObjSubtarget>();

  OutStreamer.AddBlankLine();

  // Define any .file directives
  DebugInfoFinder DbgFinder;
  DbgFinder.processModule(M);

  for (DebugInfoFinder::iterator I = DbgFinder.compile_unit_begin(),
       E = DbgFinder.compile_unit_end(); I != E; ++I) {
    DICompileUnit DIUnit(*I);
    StringRef FN = DIUnit.getFilename();
    StringRef Dir = DIUnit.getDirectory();
    GetOrCreateSourceID(FN, Dir);
  }

  OutStreamer.AddBlankLine();

  // declare external functions
  for (Module::const_iterator i = M.begin(), e = M.end();
       i != e; ++i)
    EmitFunctionDeclaration(i);

  // declare global variables
  for (Module::const_global_iterator i = M.global_begin(), e = M.global_end();
       i != e; ++i)
    EmitVariableDeclaration(i);
}

void AIObjAsmPrinter::EmitFunctionBodyStart() {
  OutStreamer.EmitRawText(Twine("{"));

  const AIObjMachineFunctionInfo *MFI = MF->getInfo<AIObjMachineFunctionInfo>();
}

void AIObjAsmPrinter::EmitFunctionBodyEnd() {
  OutStreamer.EmitRawText(Twine("}"));
}

void AIObjAsmPrinter::EmitInstruction(const MachineInstr *MI) {
  MCInst TmpInst;
  LowerAIObjMachineInstrToMCInst(MI, TmpInst, *this);
  OutStreamer.EmitInstruction(TmpInst);
}

void AIObjAsmPrinter::EmitVariableDeclaration(const GlobalVariable *gv) {
  // Check to see if this is a special global used by LLVM, if so, emit it.
  if (EmitSpecialLLVMGlobal(gv))
    return;

  MCSymbol *gvsym = Mang->getSymbol(gv);

  assert(gvsym->isUndefined() && "Cannot define a symbol twice!");

  SmallString<128> decl;
  raw_svector_ostream os(decl);

  // check if it is defined in some other translation unit
  if (gv->isDeclaration())
    os << ".extern ";

  // state space: e.g., .global

  // alignment (optional)
  unsigned alignment = gv->getAlignment();
  if (alignment != 0)
    os << ".align " << gv->getAlignment() << ' ';


  if (PointerType::classof(gv->getType())) {
    PointerType* pointerTy = dyn_cast<PointerType>(gv->getType());
    Type* elementTy = pointerTy->getElementType();

    if (elementTy->isArrayTy()) {
      assert(elementTy->isArrayTy() && "Only pointers to arrays are supported");

      ArrayType* arrayTy = dyn_cast<ArrayType>(elementTy);
      elementTy = arrayTy->getElementType();

      unsigned numElements = arrayTy->getNumElements();

      while (elementTy->isArrayTy()) {
        arrayTy = dyn_cast<ArrayType>(elementTy);
        elementTy = arrayTy->getElementType();

        numElements *= arrayTy->getNumElements();
      }

      // FIXME: isPrimitiveType() == false for i16?
      assert(elementTy->isSingleValueType() &&
             "Non-primitive types are not handled");

      // Find the size of the element in bits
      unsigned elementSize = elementTy->getPrimitiveSizeInBits();

      os << ".b" << elementSize << ' ' << gvsym->getName()
         << '[' << numElements << ']';
    } else {
      os << ".b8" << gvsym->getName() << "[]";
    }

    // handle string constants (assume ConstantArray means string)
    if (gv->hasInitializer()) {
      const Constant *C = gv->getInitializer();
      if (const ConstantArray *CA = dyn_cast<ConstantArray>(C)) {
        os << " = {";

        for (unsigned i = 0, e = C->getNumOperands(); i != e; ++i) {
          if (i > 0)
            os << ',';

          os << "0x";
          os.write_hex(cast<ConstantInt>(CA->getOperand(i))->getZExtValue());
        }

        os << '}';
      }
    }
  } else {
    // Note: this is currently the fall-through case and most likely generates
    //       incorrect code.

    if (isa<ArrayType>(gv->getType()) || isa<PointerType>(gv->getType()))
      os << "[]";
  }

  os << ';';

  OutStreamer.EmitRawText(os.str());
  OutStreamer.AddBlankLine();
}

void AIObjAsmPrinter::EmitFunctionEntryLabel() {
  // The function label could have already been emitted if two symbols end up
  // conflicting due to asm renaming.  Detect this and emit an error.
  if (!CurrentFnSym->isUndefined())
    report_fatal_error("'" + Twine(CurrentFnSym->getName()) +
                       "' label emitted multiple times to assembly file");

  const AIObjMachineFunctionInfo *MFI = MF->getInfo<AIObjMachineFunctionInfo>();
  const AIObjSubtarget& ST = TM.getSubtarget<AIObjSubtarget>();
}

void AIObjAsmPrinter::EmitFunctionDeclaration(const Function* func)
{
  const AIObjSubtarget& ST = TM.getSubtarget<AIObjSubtarget>();

  std::string decl = "";

  // hard-coded emission of extern vprintf function

  OutStreamer.EmitRawText(Twine(decl));
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

  // Print out a .file directive to specify files for .loc directives.
  OutStreamer.EmitDwarfFileDirective(SrcId, "", Entry.getKey());

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
