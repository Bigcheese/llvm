//===- Atom.cpp - Atom for linking ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/Atom.h"
#include "llvm/Object/Context.h"

using namespace llvm;
using namespace object;

Atom::Atom(unsigned Type)
  : Scope(0)
  , Definition(0)
  , Combine(0)
  , WeakImport(0)
  , Inclusion(0)
  , TypeID(Type) {
}

Atom::~Atom() {
}

PhysicalAtom::PhysicalAtom(unsigned Type)
  : Atom(Type)
  , VirtualSize(0)
  , Alignment(0, 0)
  , VirtualAddress(~uint64_t(0))
  , RelativeVirtualAddress(~uint64_t(0))
  , OutputFileAddress(~uint64_t(0))
  , InputSection(0) {
}

PhysicalAtom::~PhysicalAtom() {
}
