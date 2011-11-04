//===- Module.cpp - Object File Module --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/Context.h"
#include "llvm/Object/Module.h"
#include "llvm/Object/ObjectFile.h"

using namespace llvm;
using namespace object;

Module::Module(Context &c, OwningPtr<ObjectFile> &from, error_code &ec)
 : C(c) {
  // Not a swap because we want to null out from.
  Represents.reset(from.take());
}

Module::~Module() {}

Atom *Module::getOrCreateAtom(Name name) {
  AtomMap_t::const_iterator atom = AtomMap.find(name);
  if (atom == AtomMap.end()) {
    Atom *a = new Atom;
    a->_Name = name;
    Atoms.push_back(a);
    AtomMap.insert(std::make_pair(name, a));
    return a;
  } else
    return atom->second;
}
