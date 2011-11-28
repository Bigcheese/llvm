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
  void replaceAllUsesWith(Atom *newAtom);

  typedef std::vector<Link> LinkList_t;
  typedef SmallVector<Atom*, 1> UseList_t;

  /// @brief An enumeration for the kinds of linkage for atoms.
  enum LinkageTypes {
    UnknownLinkage = 0,
    ExternalLinkage,///< Externally visible
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
    UnknownVisibility = 0,
    DefaultVisibility,
    HiddenVisibility,
    ProtectedVisibility
  };

  Name Identifier;
  unsigned int Linkage    : 4;
  unsigned int Visibility : 2;

  LinkList_t Links;
  UseList_t  Uses;
};

class PhysicalAtom : public Atom {
protected:
  PhysicalAtom();
  virtual ~PhysicalAtom();

  StringRef Contents;
  uint64_t VirtualSize;
  unsigned int Alignment : 7; ///< log2 of atom alignment.
  uint64_t VirtualAddress;
  uint64_t RelativeVirtualAddress;
  uint64_t OutputFileAddress;
  Name OutputSegment;

  /// @brief Update the physical contents of an Atom. This also updates the
  ///        physical size.
  ///
  /// This function is to be implemented by atoms that lazily generate their
  /// contents. If the address or size of the contents has changed, Contents
  /// must be updated to point to this new data.
  virtual void updateContents() {}

  /// @brief Update the physical size of the atom without updating the contents.
  virtual void updatePhysicalSize() {}

  /// @brief Update the virtual size of an atom.
  ///
  /// The virtual size will always be >= Contents.size(). The data contained in
  /// the difference is implicitly 0.
  virtual void updateVirtualSize() {
    VirtualSize = Contents.size();
  }

public:
  /// @brief Get the contents of the atom.
  ///
  /// getContents().size() == getPhysicalSize();
  StringRef getContents() {
    updateContents();
    return Contents;
  }

  /// @brief Get the physical size of an atom. This is the minimum size it can
  ///        take on disk.
  ///
  /// Gets the physical size of an atom without updating its contents.
  uint32_t  getPhysicalSize() {
    updatePhysicalSize();
    return Contents.size();
  }

  /// @brief Get the virtual size of an atom. This is the size that the atom
  ///        shall take in memory when loaded.
  ///
  /// getVirtualSize >= getPhysicalSize();
  uint64_t  getVirtualSize() {
    updateVirtualSize();
    return VirtualSize;
  }

  /// @brief Get the log base 2 alignment of the atom.
  unsigned  getAlignment() {
    return Alignment;
  }

  /// @brief Get the output file address of the atom.
  uint64_t  getFileAddress() {
    OutputFileAddress;
  }

  /// @brief Get the virtual address of the atom.
  ///
  /// @returns ~uint64_t(0) if the virtual address is unknown.
  uint64_t  getVirtualAddress() {
    return VirtualAddress;
  }

  /// @brief Get the virtual address of the atom relative to the image base.
  ///
  /// @returns ~uint64_t(0) if the relative virtual address is unknown.
  uint64_t  getRelativeVirtualAddress() {
    return RelativeVirtualAddress;
  }
};

}
}

#endif
