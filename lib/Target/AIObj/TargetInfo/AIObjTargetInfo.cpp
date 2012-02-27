//===-- AIObjTargetInfo.cpp - AIObj Target Implementation -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AIObj.h"
#include "llvm/Module.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target llvm::TheAIObjTarget;

extern "C" void LLVMInitializeAIObjTargetInfo() {
  RegisterTarget<Triple::aiobj> X(TheAIObjTarget, "aiobj", "AIObj");
}
