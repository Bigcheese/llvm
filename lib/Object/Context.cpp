//===- Context.cpp - Object File Linking Context ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Object/Context.h"

using namespace llvm;
using namespace object;

Context::Context() {}

Context::~Context() {}

Name Context::getName(Twine name) {
  SmallString<128> storage;
  StringRef n = name.toStringRef(storage);
  // See if we already have a name.
  StringMapEntry<Name> &i = Names.GetOrCreateValue(n);
  if (i.second.data.size() == 0) {
    i.second.data = i.first();
  }
  return i.second;
}
