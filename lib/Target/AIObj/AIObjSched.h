//===-- AIObjSched.h - AIObj Register Information Impl ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the AIObj specific scheduler.
//
//===----------------------------------------------------------------------===//

#ifndef AIOBJ_SCHED_H
#define AIOBJ_SCHED_H

#include "llvm/CodeGen/ScheduleDAG.h"

namespace llvm {
ScheduleDAGSDNodes *createAIObjDAGScheduler( SelectionDAGISel *IS
                                           , CodeGenOpt::Level OptLevel);
}

#endif
