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

#include "llvm/Object/Archive.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/MemoryBuffer.h"

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
  char Size[10]; //< Size of data, not including header or padding.
  char Terminator[2];

  ///! Get the name without looking up long names.
  StringRef getName() const {
    char EndCond = Name[0] == '/' ? ' ' : '/';
    StringRef::size_type end = StringRef(Name, 16).find(EndCond);
    if (end == StringRef::npos)
      end = 16;
    assert(end <= 16 && end > 0);
    // Don't include the EndCond if there is one.
    return StringRef(Name, end);
  }

  size_t getSize() const {
    size_t ret;
    StringRef(Size, sizeof(Size)).getAsInteger(10, ret);
    return ret;
  }
};

const ArchiveMemberHeader *ToHeader(const char *base) {
  return reinterpret_cast<const ArchiveMemberHeader *>(base);
}
}

Archive::Child Archive::Child::getNext() const {
  size_t SpaceToSkip = sizeof(ArchiveMemberHeader) +
    ToHeader(Data.data())->getSize();
  // If it's odd, add 1 to make it even.
  if (SpaceToSkip & 1)
    ++SpaceToSkip;

  const char *NextLoc = Data.data() + SpaceToSkip;

  // Check to see if this is past the end of the archive.
  if (NextLoc >= Parent->Data->getBufferEnd())
    return Child(Parent, StringRef(0, 0));

  size_t NextSize = sizeof(ArchiveMemberHeader) +
    ToHeader(NextLoc)->getSize();

  return Child(Parent, StringRef(NextLoc, NextSize));
}

StringRef Archive::Child::getName() const {
  StringRef name = ToHeader(Data.data())->getName();
  // Check if it's a special name.
  if (name[0] == '/') {
    if (name.size() == 1) return name; // Linker member.
    if (name.size() == 2 && name[1] == '/') return name; // String table.
    // It's a long name.
    size_t offset;
    name.substr(1).getAsInteger(10, offset);
    // FIXME: Check that the end of the string is within the file. This isn't
    //        an issue at the moment because MemoryBuffer is null-terminated,
    //        but it's bad to rely on that.
    return StringRef(Parent->StringTable->Data.begin()
                     + sizeof(ArchiveMemberHeader)
                     + offset);
  }
  // It's a simple name.
  if (name[name.size() - 1] == '/')
    return name.substr(0, name.size() - 1);
  return name;
}

Archive::Archive(MemoryBuffer *source)
  : Binary(Binary::isArchive, source)
  , StringTable(Child(this, StringRef(0, 0))) {
  // Get the string table. It's the 3rd member.
  child_iterator StrTable = begin_children();
  child_iterator e = end_children();
  for (int i = 0; StrTable != e && i < 3; ++StrTable, ++i);

  // Check to see if there were 3 members, or the 3rd member wasn't named "//".
  if (StrTable == e || StrTable->getName() != "//")
    assert("Invalid archive! Also, this shouldn't be an assert...");

  StringTable = StrTable;
}

Archive::child_iterator Archive::begin_children() {
  const char *Loc = Data->getBufferStart() + Magic.size();
  size_t Size = sizeof(ArchiveMemberHeader) +
    ToHeader(Loc)->getSize();
  return Child(this, StringRef(Loc, Size));
}

Archive::child_iterator Archive::end_children() {
  return Child(this, StringRef(0, 0));
}

namespace llvm {

} // end namespace llvm
