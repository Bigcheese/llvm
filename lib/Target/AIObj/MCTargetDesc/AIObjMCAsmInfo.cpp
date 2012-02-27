//===-- AIObjMCAsmInfo.cpp - AIObj asm properties -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the AIObjMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "AIObjMCAsmInfo.h"
#include "llvm/ADT/Triple.h"

using namespace llvm;

void AIObjMCAsmInfo::anchor() { }

AIObjMCAsmInfo::AIObjMCAsmInfo(const Target &T, StringRef TT) {
  PointerSize = 8;
  CommentString = "//";
  LabelSuffix = "";
  PrivateGlobalPrefix = "";
  AllowPeriodsInName = false;
  HasSetDirective = false;
  HasSingleParameterDotFile = false;
}
