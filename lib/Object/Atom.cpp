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

Atom::Atom()
  : Linkage(UnknownLinkage)
  , Visibility(UnknownVisibility) {
}

Atom::~Atom() {
}

PhysicalAtom::PhysicalAtom()
  : VirtualSize(0)
  , Alignment(0)
  , VirtualAddress(~uint64_t(0))
  , RelativeVirtualAddress(~uint64_t(0))
  , OutputFileAddress(~uint64_t(0)) {
}

PhysicalAtom::~PhysicalAtom() {
}
