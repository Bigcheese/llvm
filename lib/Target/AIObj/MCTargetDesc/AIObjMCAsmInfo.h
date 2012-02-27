//===-- AIObjMCAsmInfo.h - AIObj asm properties ----------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the AIObjMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef AIOBJTARGETASMINFO_H
#define AIOBJTARGETASMINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
  class Target;

  class AIObjELFMCAsmInfo : public MCAsmInfo {
    virtual void anchor();
  public:
    explicit AIObjELFMCAsmInfo(const Target &T, StringRef TT);
  };

} // namespace llvm

#endif
