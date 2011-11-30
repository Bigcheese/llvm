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
  enum AtomKind {
    AK_Atom              = 0,
    AK_PhysicalAtom      = 1,
    AK_DLLImportDataAtom = 2,
    AK_PhysicalAtomEnd   = 3,
    AK_DLLImportAtom     = 4,
    AK_AtomEnd           = 31,
  };

  Atom(unsigned Type = AK_Atom);

  friend class Module;
  friend struct ilist_node_traits<Atom>;
  friend struct ilist_sentinel_traits<Atom>;

  virtual ~Atom();

public:
  void replaceAllUsesWith(Atom *newAtom);

  typedef std::vector<Link> LinkList_t;
  typedef SmallVector<Atom*, 1> UseList_t;

  enum ScopeTypes {
    ScopeTranslationUnit,
    ScopeLinkageUnit,
    ScopeGlobal
  };

  enum DefinitionTypes {
    DefinitionRegular,
    DefinitionTentative,
    DefinitionAbsolute,
    DefinitionProxy
  };

  enum CombineTypes {
    CombineNever,
    CombineByName,
    CombineByNameAndContent,
    CombineByNameAndReferences
  };

  enum SymbolTableInclusionTypes {
    SymbolTableNotIn,
    SymbolTableNotInFinalLinkedImages,
    SymbolTableIn,
    SymbolTableInAndNeverStrip,
    SymbolTableInAsAbsolute,
    SymbolTableInWithRandomAutoStripLabel
  };

  enum WeakImportStateTypes {
    WeakImportUnset,
    WeakImportTrue,
    WeakImportFalse
  };

  Name Identifier;
  unsigned int Scope      : 2;
  unsigned int Definition : 2;
  unsigned int Combine    : 2;
  unsigned int WeakImport : 2;
  unsigned int Inclusion  : 3;
  unsigned int TypeID     : 5;

  LinkList_t Links;
  UseList_t  Uses;

  unsigned int getType() const { return TypeID; }
  static inline bool classof(const Atom *v) { return true; }
};

class PhysicalAtom : public Atom {
public:
  struct AlignmentInfo {
    AlignmentInfo(uint8_t a, uint16_t m) : Modulus(m), PowerOf2(a) {}
    uint16_t Modulus;
    uint8_t PowerOf2;
  };

  struct Section {
    enum {
      Unclasified,
      Code,
      InitializedData,
      UninitializedData
    } Type;

    Name Identifier;
  };

protected:
  friend class Module;

  PhysicalAtom(unsigned Type = AK_PhysicalAtom);
  virtual ~PhysicalAtom();

  StringRef Contents;
  uint64_t VirtualSize;
  AlignmentInfo Alignment;
  uint64_t VirtualAddress;
  uint64_t RelativeVirtualAddress;
  uint64_t OutputFileAddress;
  const Section *InputSection;

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

  void setContents(StringRef c) {
    Contents = c;
  }

  /// @brief Get the physical size of an atom. This is the minimum size it can
  ///        take on disk.
  ///
  /// Gets the physical size of an atom without updating its contents.
  uint32_t getPhysicalSize() {
    updatePhysicalSize();
    return Contents.size();
  }

  /// @brief Get the virtual size of an atom. This is the size that the atom
  ///        shall take in memory when loaded.
  ///
  /// getVirtualSize >= getPhysicalSize();
  uint64_t getVirtualSize() {
    updateVirtualSize();
    return VirtualSize;
  }

  /// @brief Get the log base 2 alignment of the atom.
  AlignmentInfo getAlignment() const {
    return Alignment;
  }

  /// @brief Get the output file address of the atom.
  uint64_t getFileAddress() const {
    OutputFileAddress;
  }

  /// @brief Get the virtual address of the atom.
  ///
  /// @returns ~uint64_t(0) if the virtual address is unknown.
  uint64_t getVirtualAddress() const {
    return VirtualAddress;
  }

  /// @brief Get the virtual address of the atom relative to the image base.
  ///
  /// @returns ~uint64_t(0) if the relative virtual address is unknown.
  uint64_t getRelativeVirtualAddress() const {
    return RelativeVirtualAddress;
  }

  void setRelativeVirtualAddress(uint64_t rva) {
    RelativeVirtualAddress = rva;
  }

  const Section *getInputSection() const {
    return InputSection;
  }

  void setInputSection(const Section *is) {
    InputSection = is;
  }

  static inline bool classof(const Atom *v) {
    return v->getType() >= AK_PhysicalAtom
           && v->getType() < AK_PhysicalAtomEnd;
  }
  static inline bool classof(const PhysicalAtom *v) { return true; }
};

}
}

#endif
