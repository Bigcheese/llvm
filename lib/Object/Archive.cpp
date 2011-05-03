//===- Archive.cpp - ar File Format implementation --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the ArchiveObjectFile class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/ObjectFile.h"

using namespace llvm;
using namespace object;

namespace {
const StringRef Magic = "!<arch>\n";

struct ArchiveMemberHeader {
  char Name[16];
  char LastModified[12];
  char UID[6];
  char GID[6];
  char AccessMode[8];
  char Size[10];
  char Terminator[2];
};
}

namespace {
class ArchiveObjectFile : public ObjectFile {
protected:

public:
  ArchiveObjectFile(MemoryBuffer *Object);

  child_iterator begin_children();
  child_iterator end_children();

  symbol_iterator begin_symbols();
  symbol_iterator end_symbols();
};
}

ArchiveObjectFile::ArchiveObjectFile(MemoryBuffer *Object)
  : ObjectFile(Object) {
  // Nothing to see here, move along.
}

namespace llvm {

} // end namespace llvm
