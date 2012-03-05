//===-- AIObjSched.cpp - AIObj Register Information Impl --------*- C++ -*-===//
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

#include "AIObjSched.h"
#include "MCTargetDesc/AIObjMCTargetDesc.h"
#include "llvm/CodeGen/ScheduleDAGSDNodes.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

class AIObjSched : public ScheduleDAGSDNodes {
public:
  AIObjSched(SelectionDAGISel *Is) : ScheduleDAGSDNodes(*Is->MF), IS(Is) {}

  // Schedule based on a depth first search.
  void ScheduleDFS(SDNode &SD) {
    if (isPassiveNode(&SD))
      return;
    // Emit load if this is already in a stack slot.
    if (StackSlots.find(&SD) != StackSlots.end()) {
      Sequence.push_back(NewSUnit(DAG->getMachineNode(AIObj::LOAD_FROM_STACK_SLOT, SD.getDebugLoc(), MVT::Other)));
      return;
    }
    for (unsigned i = 0, e = SD.getNumOperands(); i != e; ++i) {
      const SDValue &SDV = SD.getOperand(i);
      if (e > 1 && SDV.getValueType() == MVT::Other)
        continue;
      ScheduleDFS(*SDV.getNode());
    }
    Sequence.push_back(NewSUnit(&SD));
    // Create a store to temp if this has more than one use.
    unsigned UseCount = 0;
    for (auto i = SD.use_begin(), e = SD.use_end(); i != e; ++i) {
      if (i->getValueType(0) != MVT::Other)
        ++UseCount;
    }
    if (UseCount > 1) {
      Sequence.push_back(NewSUnit(DAG->getMachineNode(AIObj::STORE_TO_STACK_SLOT, SD.getDebugLoc(), MVT::Other)));
      StackSlots[&SD] = 0;
    }
  }

  void Schedule() {
    unsigned NumSUnits = 0;
    for (SelectionDAG::allnodes_iterator NI = DAG->allnodes_begin(),
           E = DAG->allnodes_end(); NI != E; ++NI) {
      NI->setNodeId(-1);
      ++NumSUnits;
    }
    SUnits.reserve(NumSUnits * 2);
    ScheduleDFS(*DAG->getRoot().getNode());
  }

private:
  SelectionDAGISel *IS;
  DenseMap<SDNode*, unsigned> StackSlots;
};

ScheduleDAGSDNodes *llvm::createAIObjDAGScheduler( SelectionDAGISel *IS
                                                 , CodeGenOpt::Level OptLevel){
  return new AIObjSched(IS);
}
