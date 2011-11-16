//===- Atom.cpp - Atom for linking ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ATOM_H
#define LLVM_OBJECT_ATOM_H

#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Context.h"
#include "llvm/Support/DataTypes.h"
#include <vector>

namespace llvm {
namespace object {
class Atom;
class Module;

// Discriminated union.
class Link {
public:
  typedef SmallVector<Atom*, 1> OperandList_t;
  typedef OperandList_t::iterator operand_iterator;

  OperandList_t Operands;

  enum LinkType {
    LT_Relocation,
    LT_LocationOffsetConstraint,
    LT_ResolvedTo
  } Type;

  uint32_t ConstraintDistance;
  uint64_t RelocAddr;
  uint64_t RelocType;
};

class Atom : public ilist_node<Atom> {
  Atom(const Atom&); // = delete;
  Atom &operator=(const Atom&); // = delete;

protected:
  Atom();

  friend class Module;
  friend struct ilist_node_traits<Atom>;
  friend struct ilist_sentinel_traits<Atom>;

  virtual ~Atom();

public:
  typedef std::vector<Link> LinkList_t;

  enum {
    AT_Unknown,
    AT_Code,
    AT_Data,
    AT_Import
  } Type;
  unsigned Defined  : 1;
  unsigned External : 1;
  StringRef Contents;
  Name _Name;
  Name ImportFrom;
  LinkList_t Links;
  uint64_t RVA;
};

}
}

#endif
