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
  : Type(AT_Unknown)
  , Defined(false)
  , External(false) {
}

Atom::~Atom() {
}
