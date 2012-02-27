//===-- AIObjISelLowering.cpp - AIObj DAG Lowering Implementation ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the interfaces that AIObj uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "AIObjISelLowering.h"
#include "AIObjTargetMachine.h"
#include "AIObjMachineFunctionInfo.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;


//===----------------------------------------------------------------------===//
// Calling Convention Implementation
//===----------------------------------------------------------------------===//

#include "AIObjGenCallingConv.inc"

SDValue
AIObjTargetLowering::LowerReturn(SDValue Chain,
                                 CallingConv::ID CallConv, bool isVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 DebugLoc dl, SelectionDAG &DAG) const {
  return DAG.getNode(AIOBJISD::EXIT_HANDLER, dl, MVT::Other, Chain);
}

SDValue AIObjTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
    default:
      llvm_unreachable("Unimplemented operand");
    case ISD::GlobalAddress:
      return LowerGlobalAddress(Op, DAG);
  }
}

/// LowerFormalArguments - V8 uses a very simple ABI, where all values are
/// passed in either one or two GPRs, including FP values.  TODO: we should
/// pass FP values in FP registers for fastcc functions.
SDValue
AIObjTargetLowering::LowerFormalArguments
                  ( SDValue Chain
                  , CallingConv::ID CallConv
                  , bool isVarArg
                  , const SmallVectorImpl<ISD::InputArg> &Ins
                  , DebugLoc dl
                  , SelectionDAG &DAG
                  , SmallVectorImpl<SDValue> &InVals) const {
  if (isVarArg) llvm_unreachable("AIObj does not support varargs");

  MachineFunction &MF = DAG.getMachineFunction();
  const AIObjSubtarget &ST = getTargetMachine().getSubtarget<AIObjSubtarget>();
  AIObjMachineFunctionInfo *MFI = MF.getInfo<AIObjMachineFunctionInfo>();

  for (unsigned i = 0, e = Ins.size(); i != e; ++i) {
    InVals.push_back(
      DAG.getCopyFromReg( Chain
                        , dl
                        , MF.getRegInfo().createVirtualRegister(
                            AIObj::RegI64RegisterClass)
                        , Ins[i].VT));
  }

  return Chain;
}

SDValue
AIObjTargetLowering::LowerCall(SDValue Chain, SDValue Callee,
                               CallingConv::ID CallConv, bool isVarArg,
                               bool &isTailCall,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               DebugLoc dl, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  const AIObjSubtarget &ST = getTargetMachine().getSubtarget<AIObjSubtarget>();
  AIObjMachineFunctionInfo *MFI = MF.getInfo<AIObjMachineFunctionInfo>();

  std::vector<SDValue> Ops;
  // The layout of the ops will be [Chain, #Ins, Ins, Callee, #Outs, Outs]
  Ops.push_back(Chain);
  Ops.push_back(DAG.getTargetConstant(Ins.size(), MVT::i64));

  for (unsigned i = 0, e = Ins.size(); i != e; ++i) {
    unsigned VReg =
      MF.getRegInfo().createVirtualRegister(AIObj::RegI64RegisterClass);
    SDValue ArgValue = DAG.getCopyFromReg(Chain, dl, VReg, Ins[i].VT);
    InVals.push_back(ArgValue);
    Ops.push_back(ArgValue);
  }

  Ops.push_back(Callee);
  Ops.push_back(DAG.getTargetConstant(Outs.size(), MVT::i64));

  for (unsigned i = 0, e = Outs.size(); i != e; ++i) {
    Ops.push_back(OutVals[i]);
  }

  return DAG.getNode(AIOBJISD::FUNCTION_CALL, dl, MVT::Other, &Ops[0], Ops.size());
}

unsigned
AIObjTargetLowering::getSRetArgSize(SelectionDAG &DAG, SDValue Callee) const
{
  llvm_unreachable("Unimplemented");
}

//===----------------------------------------------------------------------===//
// TargetLowering Implementation
//===----------------------------------------------------------------------===//

AIObjTargetLowering::AIObjTargetLowering(TargetMachine &TM)
  : TargetLowering(TM, new TargetLoweringObjectFileELF()) {

  // Set up the register classes.
  addRegisterClass(MVT::i64, &AIObj::RegI64RegClass);

  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);

  setMinFunctionAlignment(2);

  computeRegisterProperties();
}

const char *AIObjTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  default: return 0;
  case AIOBJISD::FUNCTION_CALL: return "AIOBJISD::FUNCTION_CALL";
  case AIOBJISD::EXIT_HANDLER:  return "AIOBJISD::EXIT_HANDLER";
  }
}

SDValue AIObjTargetLowering::LowerGlobalAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  return DAG.getTargetConstant(0xdeadbeef, MVT::i64);
}

SDValue AIObjTargetLowering::LowerConstantPool(SDValue Op,
                                               SelectionDAG &DAG) const {
  llvm_unreachable("Unimplemented");
}

MachineBasicBlock *
AIObjTargetLowering::EmitInstrWithCustomInserter(MachineInstr *MI,
                                                 MachineBasicBlock *BB) const {
  llvm_unreachable("Unimplemented!");
}

//===----------------------------------------------------------------------===//
//                         AIObj Inline Assembly Support
//===----------------------------------------------------------------------===//

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
AIObjTargetLowering::ConstraintType
AIObjTargetLowering::getConstraintType(const std::string &Constraint) const {
  llvm_unreachable("Unimplemented");
}

std::pair<unsigned, const TargetRegisterClass*>
AIObjTargetLowering::getRegForInlineAsmConstraint(const std::string &Constraint,
                                                  EVT VT) const {
  llvm_unreachable("Unimplemented");
}

bool
AIObjTargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  llvm_unreachable("Unimplemented");
}
