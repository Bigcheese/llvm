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
  NameMap_t::const_iterator i = Names.find(n);
  if (i == Names.end()) {
    // Setup the data.
    char *data = reinterpret_cast<char*>(
                   Allocate(n.size() + sizeof(Name::Data),
                            AlignOf<Name::Data>::Alignment));
    reinterpret_cast<Name::Data*>(data)->Length = n.size();
    std::memcpy(data + sizeof(Name::Data), n.data(), n.size());
    Name ret;
    ret.data = data;
    // Note that the StringRef stored in the map references allocated memory.
    Names[ret.str()] = ret;
    return ret;
  }
  return i->second;
}
