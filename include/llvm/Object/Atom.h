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

  /// @brief An enumeration for the kinds of linkage for atoms.
  enum LinkageTypes {
    ExternalLinkage = 0,///< Externally visible
    LinkOnceAnyLinkage, ///< Keep one copy of function when linking (inline)
    LinkOnceODRLinkage, ///< Same, but only replaced by something equivalent.
    WeakAnyLinkage,     ///< Keep one copy of named function when linking (weak)
    WeakODRLinkage,     ///< Same, but only replaced by something equivalent.
    AppendingLinkage,   ///< Special purpose, only applies to global arrays
    InternalLinkage,    ///< Rename collisions when linking (static functions).
    PrivateLinkage,     ///< Like Internal, but omit from symbol table.
    DLLImportLinkage,   ///< Function to be imported from DLL
    DLLExportLinkage,   ///< Function to be accessible from DLL.
    ExternalWeakLinkage,///< ExternalWeak linkage description.
    CommonLinkage       ///< Tentative definitions.
  };

  /// @brief An enumeration for the kinds of visibility of atoms.
  enum VisibilityTypes {
    DefaultVisibility = 0,
    HiddenVisibility,
    ProtectedVisibility
  };

  Name Name;
  unsigned int Linkage    : 4;
  unsigned int Visibility : 2;
};

class PhysicalAtom : public Atom {
private:
  virtual ~PhysicalAtom();

  unsigned int Alignment : 7; ///< log2 of atom alignment.
  uint64_t VirtualAddress;
  uint64_t RelativeVirtualAddress;
  uint64_t VirtualSize;
  uint64_t OutputFileAddress;
  Name OutputSegment;

public:

  virtual StringRef getContents();
  virtual uint64_t  getVirtualSize();


};

}
}

#endif
