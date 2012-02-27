//===-- AIObjSelectionDAGInfo.cpp - AIObj SelectionDAG Info ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the AIObjSelectionDAGInfo class.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "sparc-selectiondag-info"
#include "AIObjTargetMachine.h"
using namespace llvm;

AIObjSelectionDAGInfo::AIObjSelectionDAGInfo(const AIObjTargetMachine &TM)
  : TargetSelectionDAGInfo(TM) {
}

AIObjSelectionDAGInfo::~AIObjSelectionDAGInfo() {
}
