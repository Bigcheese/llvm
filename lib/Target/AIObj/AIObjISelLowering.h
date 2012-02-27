//===-- AIObjISelLowering.h - AIObj DAG Lowering Interface ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that AIObj uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef AIOBJ_ISELLOWERING_H
#define AIOBJ_ISELLOWERING_H

#include "llvm/Target/TargetLowering.h"
#include "AIObj.h"

namespace llvm {
  namespace AIOBJISD {
    enum {
      FIRST_NUMBER = ISD::BUILTIN_OP_END,
      FUNCTION_CALL,
      EXIT_HANDLER
    };
  }

  class AIObjTargetLowering : public TargetLowering {
  public:
    AIObjTargetLowering(TargetMachine &TM);
    virtual SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const;

    virtual MachineBasicBlock *
      EmitInstrWithCustomInserter(MachineInstr *MI,
                                  MachineBasicBlock *MBB) const;

    virtual const char *getTargetNodeName(unsigned Opcode) const;

    ConstraintType getConstraintType(const std::string &Constraint) const;
    std::pair<unsigned, const TargetRegisterClass*>
    getRegForInlineAsmConstraint(const std::string &Constraint, EVT VT) const;

    virtual bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const;

    virtual SDValue
      LowerFormalArguments(SDValue Chain,
                           CallingConv::ID CallConv,
                           bool isVarArg,
                           const SmallVectorImpl<ISD::InputArg> &Ins,
                           DebugLoc dl, SelectionDAG &DAG,
                           SmallVectorImpl<SDValue> &InVals) const;

    virtual SDValue
      LowerCall(SDValue Chain, SDValue Callee,
                CallingConv::ID CallConv, bool isVarArg,
                bool &isTailCall,
                const SmallVectorImpl<ISD::OutputArg> &Outs,
                const SmallVectorImpl<SDValue> &OutVals,
                const SmallVectorImpl<ISD::InputArg> &Ins,
                DebugLoc dl, SelectionDAG &DAG,
                SmallVectorImpl<SDValue> &InVals) const;

    virtual SDValue
      LowerReturn(SDValue Chain,
                  CallingConv::ID CallConv, bool isVarArg,
                  const SmallVectorImpl<ISD::OutputArg> &Outs,
                  const SmallVectorImpl<SDValue> &OutVals,
                  DebugLoc dl, SelectionDAG &DAG) const;

    SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;

    unsigned getSRetArgSize(SelectionDAG &DAG, SDValue Callee) const;
  };
} // end namespace llvm

#endif    // AIOBJ_ISELLOWERING_H
