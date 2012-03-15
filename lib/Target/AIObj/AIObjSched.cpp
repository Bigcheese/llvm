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
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/CodeGen/ScheduleDAGSDNodes.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Support/raw_ostream.h"

#include <functional>
#include <set>

using namespace llvm;

class AIObjSched : public ScheduleDAGSDNodes {
public:
  AIObjSched(SelectionDAGISel *Is)
    : ScheduleDAGSDNodes(*Is->MF)
    , IS(Is)
    , CurrentStackSlot(0) {}

  // Schedule based on a depth first search.
  void ScheduleDFS(SDNode &SD) {
    if (isPassiveNode(&SD))
      return;
    // Emit load if this is already in a stack slot.
    DenseMap<SDNode*, unsigned>::iterator i = StackSlots.find(&SD);
    if (i != StackSlots.end()) {
      Sequence.push_back(
        newSUnit(
          DAG->getMachineNode(
              AIObj::LOAD_FROM_STACK_SLOT
            , SD.getDebugLoc()
            , MVT::Other
            , DAG->getTargetConstant(i->second, MVT::i32)
            )
        )
      );
      return;
    }
    for (unsigned i = 0, e = SD.getNumOperands(); i != e; ++i) {
      const SDValue &SDV = SD.getOperand(i);
      if (SDV.getValueType() == MVT::Other)
        continue;
      ScheduleDFS(*SDV.getNode());
    }
    Sequence.push_back(newSUnit(&SD));
    // Create a store to temp if this has more than one use.
    unsigned UseCount = 0;
    for (auto i = SD.use_begin(), e = SD.use_end(); i != e; ++i) {
      if (i.getUse().getValueType() != MVT::Other)
        ++UseCount;
    }
    if (UseCount > 1) {
      Sequence.push_back(
        newSUnit(
          DAG->getNode(
              ISD::CopyToReg
            , SD.getDebugLoc()
            , MVT::Other
            , DAG->getEntryNode()
            , DAG->getRegister(CurrentStackSlot, MVT::i64)
            , SDValue(&SD, 0)
            ).getNode()
        )
      );
      StackSlots[&SD] = CurrentStackSlot++;
    }
  }

  // All nodes which have no non-chain uses are added to a root node list. This
  // list is then sorted by topological order.
  //
  // For each root node, each non-chain operand is visited.
  //
  // If the operand node has more than one non-chain use it is looked up to see
  // if it has already been evaluated. If so, it is loaded from the previous
  // stack slot. If not, each non-chain operand is recursively visited in depth
  // first order.
  //
  // After visiting operands, the current node is appended to the schedule. If
  // it has more than one non-chain use, it is stored to a stack slot.
  void Schedule() {
    std::vector<SDNode*> Roots;
    unsigned NumSUnits = 0;
    for (SelectionDAG::allnodes_iterator NI = DAG->allnodes_begin(),
           E = DAG->allnodes_end(); NI != E; ++NI) {
      unsigned NonChainUses = 0;
      for (SDNode::use_iterator i = NI->use_begin(), e = NI->use_end();
                                i != e; ++i) {
        if (i.getUse().getValueType() != MVT::Other)
          ++NonChainUses;
      }
      if (NonChainUses == 0)
        Roots.push_back(NI);
      NI->setNodeId(-1);
      ++NumSUnits;
    }
    SUnits.reserve(NumSUnits * 2);

    std::vector<SDNode*> Result;
    std::vector<SDNode*> Visited;

    std::function<void(SDNode*)> POS = [&](SDNode *N) {
      if (std::find(Visited.begin(), Visited.end(), N) != Visited.end())
        return;
      for (auto i = N->op_begin(), e = N->op_end(); i != e; ++i) {
        if (i->getValueType() == MVT::Other)
          POS(i->getNode());
      }
      // The current node is actually a root.
      if (std::find(Roots.begin(), Roots.end(), N) != Roots.end()) {
        Result.push_back(N);
      }
      Visited.push_back(N);
    };

    POS(DAG->getRoot().getNode());

    for (std::vector<SDNode*>::iterator i = Result.begin(), e = Result.end();
                                        i != e; ++i) {
      ScheduleDFS(**i);
    }
  }

private:
  SelectionDAGISel *IS;
  DenseMap<SDNode*, unsigned> StackSlots;
  unsigned CurrentStackSlot;
};

ScheduleDAGSDNodes *llvm::createAIObjDAGScheduler( SelectionDAGISel *IS
                                                 , CodeGenOpt::Level OptLevel){
  return new AIObjSched(IS);
}
