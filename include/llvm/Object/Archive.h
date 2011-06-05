//===- Archive.h - ar archive file format -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ar archive file format class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ARCHIVE_H
#define LLVM_OBJECT_ARCHIVE_H

#include "llvm/Object/Binary.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
namespace object {

class Archive : public Binary {
public:
  class Child {
    Archive *Parent;
    StringRef Data;

  public:
    Child(Archive *p, StringRef d) : Parent(p), Data(d) {}

    bool operator ==(const Child &other) const {
      return (Parent == other.Parent) && (Data.begin() == other.Data.begin());
    }

    Child getNext() const;
    StringRef getName() const;
    int getLastModified() const;
    int getUID() const;
    int getGID() const;
    int getAccessMode() const;
    ///! Return the size of the archive member without the header or padding.
    size_t getSize() const;

    Binary *getAsBinary() const;
  };

  class child_iterator {
    Child child;
  public:
    child_iterator(Child &c) : child(c) {}
    const Child* operator->() const {
      return &child;
    }

    bool operator==(const child_iterator &other) const {
      return child == other.child;
    }

    bool operator!=(const child_iterator &other) const {
      return !(*this == other);
    }

    child_iterator& operator++() {  // Preincrement
      child = child.getNext();
      return *this;
    }
  };

  Archive(MemoryBuffer *source);

  child_iterator begin_children();
  child_iterator end_children();

  // Cast methods.
  static inline bool classof(Archive const *v) { return true; }
  static inline bool classof(Binary const *v) {
    return v->getType() == Binary::isArchive;
  }

private:
  child_iterator StringTable;
};

}
}

#endif
