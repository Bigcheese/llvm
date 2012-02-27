//===-- AIObjMCTargetDesc.h - AIObj Target Descriptions ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides AIObj specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef AIOBJMCTARGETDESC_H
#define AIOBJMCTARGETDESC_H

namespace llvm {
class MCSubtargetInfo;
class Target;
class StringRef;

extern Target TheAIObjTarget;
} // End llvm namespace

// Defines symbolic names for AIObj registers.  This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "AIObjGenRegisterInfo.inc"

// Defines symbolic names for the AIObj instructions.
//
#define GET_INSTRINFO_ENUM
#include "AIObjGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "AIObjGenSubtargetInfo.inc"

#endif
