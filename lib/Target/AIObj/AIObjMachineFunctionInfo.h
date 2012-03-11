//===- AIObjMachineFunctionInfo.h - AIObj Machine Function Info -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares  AIObj specific per-machine-function information.
//
//===----------------------------------------------------------------------===//
#ifndef AIOBJMACHINEFUNCTIONINFO_H
#define AIOBJMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

  class AIObjMachineFunctionInfo : public MachineFunctionInfo {
    virtual void anchor();

  public:
    AIObjMachineFunctionInfo() {}
    explicit AIObjMachineFunctionInfo(MachineFunction &MF) {}
  };
}

#endif
