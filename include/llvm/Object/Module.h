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
#include <map>

namespace llvm {
class raw_ostream;
class error_code;

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
  Name ObjName;
  /// @brief Create an empty module.
  Module(Context &c) : C(c) {}

  /// @brief Create a module and read atoms from @arg from into it. If ec, the
  ///        module is empty.
  Module(Context &c, OwningPtr<ObjectFile> &from, error_code &ec);
  ~Module();

  error_code mergeObject(ObjectFile *obj);
  void mergeModule(Module *m);

  Context &getContext() const { return C; }
  Atom *getOrCreateAtom(Name name);

  /// @brief Create an anonymous atom.
  ///
  /// @param name If given, the given name will be the same. This means that
  ///             multiple atoms can have the same name.
  Atom *createAtom(Name name = Name());

  /// @brief Replace all uses of @arg a with @arg new and return @arg a.
  Atom *replaceAllUsesWith(Atom *a, Atom *New);

  atom_iterator atom_begin() { return Atoms.begin(); }
  atom_iterator atom_end()   { return Atoms.end(); }

  void printGraph(raw_ostream &o);
};

} // end namespace llvm
} // end namespace object

#endif
