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
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/RegAllocRegistry.h"

using namespace llvm;

namespace {
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
      // We do not actually do anything (at least not yet).
      return false;
    }
  };

  char AIObjRegAlloc::ID = 0;

  static RegisterRegAlloc
    aiobjRegAlloc("AIObj", "AIObj register allocator", createAIObjRegisterAllocator);
}

FunctionPass *llvm::createAIObjRegisterAllocator() {
  return new AIObjRegAlloc();
}
