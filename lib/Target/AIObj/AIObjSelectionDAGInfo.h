//===-- AIObjSelectionDAGInfo.h - AIObj SelectionDAG Info -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the AIObj subclass for TargetSelectionDAGInfo.
//
//===----------------------------------------------------------------------===//

#ifndef AIOBJSELECTIONDAGINFO_H
#define AIOBJSELECTIONDAGINFO_H

#include "llvm/Target/TargetSelectionDAGInfo.h"

namespace llvm {

class AIObjTargetMachine;

class AIObjSelectionDAGInfo : public TargetSelectionDAGInfo {
public:
  explicit AIObjSelectionDAGInfo(const AIObjTargetMachine &TM);
  ~AIObjSelectionDAGInfo();
};

}

#endif
