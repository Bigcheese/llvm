//===-- AIObjSubtarget.cpp - AIOBJ Subtarget Information ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the AIOBJ specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "AIObjSubtarget.h"
#include "AIObj.h"
#include "llvm/Support/TargetRegistry.h"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "AIObjGenSubtargetInfo.inc"

using namespace llvm;

void AIObjSubtarget::anchor() { }

AIObjSubtarget::AIObjSubtarget( const std::string &TT
                              , const std::string &CPU
                              , const std::string &FS)
  : AIObjGenSubtargetInfo(TT, CPU, FS) {
  ParseSubtargetFeatures(CPU, FS);
}
