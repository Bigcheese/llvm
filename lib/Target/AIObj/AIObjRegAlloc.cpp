//===-- AIObjRegAlloc.cpp - AIObj Register Allocator ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a register allocator for AIObj code.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "aiobj-reg-alloc"

#include "AIObj.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetInstrInfo.h"

using namespace llvm;

namespace {
  bool canFoldToDup(const MachineInstr *MI, uint64_t StackSlot) {
    if (MI->getOpcode() == AIObj::LOAD_FROM_STACK_SLOT) {
      return MI->getOperand(0).getImm() == StackSlot;
    }
    return false;
  }

  // Special register allocator for AIObj.
  class AIObjRegAlloc : public MachineFunctionPass {
  public:
    static char ID;
    AIObjRegAlloc() : MachineFunctionPass(ID) {}

    virtual const char* getPassName() const {
      return "AIObj Register Allocator";
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesCFG();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    virtual bool runOnMachineFunction(MachineFunction &MF) {
      const TargetInstrInfo &TII = *MF.getTarget().getInstrInfo();
      for (MachineFunction::iterator MBBI = MF.begin(), MBBE = MF.end();
                                     MBBI != MBBE; ++MBBI) {
        for (MachineBasicBlock::iterator MII = MBBI->instr_begin(), MIE = MBBI->instr_end();
                                                MII != MIE; ++MII) {
          if (MII->getOpcode() == AIObj::STORE_TO_STACK_SLOT) {
            int64_t StackSlot = MII->getOperand(0).getImm();
            int64_t Uses = MII->getOperand(1).getImm();
            if (canFoldToDup(MII + 1, StackSlot) && Uses == 1) {
              MII = MBBI->erase_instr(MII);
              MII = MBBI->erase_instr(MII); // Erase LOAD_FROM_STACK_SLOT
              MII = BuildMI(*MBBI, MII, MII->getDebugLoc(), TII.get(AIObj::PUSH_REG_SP));
              MII = BuildMI(*MBBI, llvm::next(MII), MII->getDebugLoc(), TII.get(AIObj::FETCH_I));
              continue;
            }
            MII = MBBI->erase_instr(MII);
            MII = BuildMI(*MBBI, MII, MII->getDebugLoc(), TII.get(AIObj::PUSH_EVENT));
            MII = BuildMI(*MBBI, llvm::next(MII), MII->getDebugLoc(), TII.get(AIObj::PUSH_CONST))
                    .addReg(0)
                    .addImm(280 + (StackSlot * 8));
            MII = BuildMI(*MBBI, llvm::next(MII), MII->getDebugLoc(), TII.get(AIObj::ADD));
            MII = BuildMI(*MBBI, llvm::next(MII), MII->getDebugLoc(), TII.get(AIObj::PUSH_REG_SP));
            MII = BuildMI(*MBBI, llvm::next(MII), MII->getDebugLoc(), TII.get(AIObj::PUSH_CONST))
                    .addReg(0)
                    .addImm(-8);
            MII = BuildMI(*MBBI, llvm::next(MII), MII->getDebugLoc(), TII.get(AIObj::ADD));
            MII = BuildMI(*MBBI, llvm::next(MII), MII->getDebugLoc(), TII.get(AIObj::FETCH_I));
            MII = BuildMI(*MBBI, llvm::next(MII), MII->getDebugLoc(), TII.get(AIObj::ASSIGN));
          } else if (MII->getOpcode() == AIObj::LOAD_FROM_STACK_SLOT) {
            uint64_t StackSlot = MII->getOperand(0).getImm();
            MII = MBBI->erase_instr(MII);
            MII = BuildMI(*MBBI, MII, MII->getDebugLoc(), TII.get(AIObj::PUSH_EVENT));
            MII = BuildMI(*MBBI, llvm::next(MII), MII->getDebugLoc(), TII.get(AIObj::PUSH_CONST))
                    .addReg(0)
                    .addImm(280 + (StackSlot * 8));
            MII = BuildMI(*MBBI, llvm::next(MII), MII->getDebugLoc(), TII.get(AIObj::ADD));
            MII = BuildMI(*MBBI, llvm::next(MII), MII->getDebugLoc(), TII.get(AIObj::FETCH_I));
          } else if (MII->getOpcode() == AIObj::FUNCTION_CALL) {
            int64_t Ops = MII->getNumOperands();
            if (Ops > 3)
              BuildMI(*MBBI, llvm::next(MII), MII->getDebugLoc(), TII.get(AIObj::SHIFT_SP)).addImm(-(Ops - 3));
          }
        }
      }
      return true;
    }
  };

  char AIObjRegAlloc::ID = 0;

  static RegisterRegAlloc
    aiobjRegAlloc("AIObj", "AIObj register allocator", createAIObjRegisterAllocator);
}

FunctionPass *llvm::createAIObjRegisterAllocator() {
  return new AIObjRegAlloc();
}
