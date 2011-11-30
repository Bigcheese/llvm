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

  template<class T>
  T *getOrCreateAtom(Name name) {
    AtomMap_t::const_iterator atom = AtomMap.find(name);
    if (atom == AtomMap.end()) {
      T *a = new T;
      a->Identifier = name;
      Atoms.push_back(a);
      AtomMap.insert(std::make_pair(name, a));
      return a;
    } else
      return cast<T>(atom->second);
  }

  /// @brief Create an anonymous atom.
  ///
  /// @param name If given, the given name will be the same. This means that
  ///             multiple atoms can have the same name.
  template<class T>
  T *createAtom(Name name = Name()) {
    T *a = new T;
    if (name.str().size() == 0)
      a->Identifier = C.getName(Twine("atom") + Twine(intptr_t(a)));
    else
      a->Identifier = name;
    Atoms.push_back(a);
    // Don't add to AtomMap, as it should not be looked up by name.
    return a;
  }

  /// @brief Replace all uses of @arg a with @arg new and return @arg a.
  Atom *replaceAllUsesWith(Atom *a, Atom *New);

  atom_iterator atom_begin() { return Atoms.begin(); }
  atom_iterator atom_end()   { return Atoms.end(); }
  atom_iterator erase(Atom *a) { return Atoms.erase(a); }

  void printGraph(raw_ostream &o);
};

} // end namespace llvm
} // end namespace object

#endif
