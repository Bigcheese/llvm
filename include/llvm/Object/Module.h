//===- Module.h - Object File Module ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_MODULE_H
#define LLVM_OBJECT_MODULE_H

#include "llvm/ADT/ilist.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Atom.h"
#include "llvm/Support/system_error.h"
#include <map>

namespace llvm {
namespace object {
class Context;
class Name;
class ObjectFile;

class Module {
public:
  typedef iplist<Atom> AtomList_t;
  typedef AtomList_t::iterator atom_iterator;
  typedef std::map<Name, Atom*> AtomMap_t;

private:
  Module(const Module&); // = delete;
  Module &operator=(const Module&); // = delete;


  Context &C;
  AtomList_t Atoms;
  AtomMap_t AtomMap;
  OwningPtr<ObjectFile> Represents;

public:
  Module(Context &c, OwningPtr<ObjectFile> &from, error_code &ec);
  ~Module();

  Context &getContext() const { return C; }
  Atom *getOrCreateAtom(Name name);

  atom_iterator atom_begin() { return Atoms.begin(); }
  atom_iterator atom_end()   { return Atoms.end(); }
};

} // end namespace llvm
} // end namespace object

#endif
