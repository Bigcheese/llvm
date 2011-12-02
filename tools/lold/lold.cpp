//===-- lold.cpp - llvm object link editor ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a linker.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Atom.h"
#include "llvm/Object/Context.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/COFF.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <sstream>
#include <vector>
using namespace llvm;
using namespace object;
using namespace support;

static std::string ToolName;

static cl::list<std::string>
  InputFilenames(cl::Positional,
                 cl::desc("<input object files>"),
                 cl::ZeroOrMore);

static void error(Twine message, Twine path = Twine()) {
  errs() << ToolName << ": " << path << ": " << message << ".\n";
}

static bool error(error_code ec, Twine path = Twine()) {
  if (ec) {
    error(ec.message(), path);
    return true;
  }
  return false;
}

struct AtomLocator {
  struct Object {
    ObjectFile *Obj;
  };

  struct ArchiveMember {
    Archive *Arch;
  };

  struct LibraryImport {
  };

  enum {
    ALT_None, ///< No way to look this up.
    ALT_Object,
    ALT_ArchiveMember,
    ALT_LibraryImport
  } Type;

  union {
    Object ObjectInfo;
    ArchiveMember ArchiveInfo;
    LibraryImport LibraryInfo;
  };
  // Can't be a member of a union :(.
  Archive::child_iterator Member;
};

struct InputSymbol {
  Name Identifier;
  Atom *Instance;
  uint32_t Priority;
  AtomLocator Location;

  bool operator <(const InputSymbol &other) {
    if (Identifier < other.Identifier)
      return true;
    else if (Identifier == other.Identifier && Priority < other.Priority)
      return true;
    return false;
  }
};

class FileReader {
public:
};

class TargetInfo;

class InputSymbolTable {
public:
  typedef std::vector<InputSymbol> Symtab_t;
  typedef Symtab_t::iterator iterator;
  typedef Symtab_t::const_iterator const_iterator;

private:
  TargetInfo &Target;
  Symtab_t Symtab;
  bool Sorted;

  void sort() {
    if (!Sorted)
      std::sort(Symtab.begin(), Symtab.end());
    Sorted = true;
  }

  static bool FindAtomByName(const InputSymbol &is, Name n) {
    return is.Identifier < n;
  }

public:
  InputSymbolTable(TargetInfo &ti)
    : Target(ti)
    , Sorted(true) {}

  iterator begin() {
    sort();
    return Symtab.begin();
  }

  iterator end() {
    return Symtab.end();
  }

  iterator add(const InputSymbol &is) {
    Sorted = false;
    Symtab.push_back(is);
    return --Symtab.end();
  }

  iterator lookup(Name n);
};

class TargetInfo {
protected:
  Context &C;

public:
  TargetInfo(Context &c) : C(c) {}

  /// @brief Fill the input symbol table with the external symbols in path.
  virtual void readExternalSymbols(InputSymbolTable &ist, Twine path) = 0;

  /// @brief Create a FileReader for the file at al. This opens the file,
  ///        figures out what type it is, and constructs a file object which it
  ///        owns.
  virtual FileReader *createReader(const AtomLocator &al) = 0;

  /// @brief Attempt to lookup a symbol. This allows the target to create
  ///        target specific symbols and atoms.
  virtual InputSymbolTable::iterator lookupSymbol( InputSymbolTable &ist
                                                 , Name n) = 0;
};

InputSymbolTable::iterator InputSymbolTable::lookup(Name n) {
  sort();
  iterator i = std::lower_bound( Symtab.begin()
                                , Symtab.end()
                                , n
                                , FindAtomByName);
  if (i != end() && i->Identifier != n)
    return Target.lookupSymbol(*this, n);
  return i;
}

class TargetInfoMicrosoft : public TargetInfo {
public:
  TargetInfoMicrosoft(Context &c) : TargetInfo(c) {}

  virtual void readExternalSymbols(InputSymbolTable &ist, Twine path) {
    bool exists;
    if (error(sys::fs::exists(path, exists), path)) {
      return;
    } else if (!exists) {
      error("does not exist", path);
      return;
    }

    SmallString<0> storage;
    StringRef p = path.toStringRef(storage);

    OwningPtr<Binary> binary;
    if (error(createBinary(p, binary), p)) {
      return;
    }

    if (Archive *a = dyn_cast<Archive>(binary.get())) {
      binary.take();
      for (Archive::symbol_iterator i = a->begin_symbols(),
                                    e = a->end_symbols(); i != e; ++i) {
        StringRef name;
        Archive::child_iterator child;
        if (error(i->getName(name), p)) continue;
        if (error(i->getMember(child), p)) continue;
        InputSymbol is;
        is.Identifier = C.getName(name);
        is.Location.Type = AtomLocator::ALT_ArchiveMember;
        is.Location.ArchiveInfo.Arch = a;
        is.Location.Member = child;
        ist.add(is);
      }
    } else if (ObjectFile *o = dyn_cast<ObjectFile>(binary.get())) {
      binary.take();
      error_code ec;
      for (symbol_iterator i = o->begin_symbols(),
                           e = o->end_symbols(); i != e; i.increment(ec)) {
        if (error(ec, p)) return;
        bool global;
        StringRef name;
        SymbolRef::Type type;
        if (error(i->isGlobal(global), p)) continue;
        if (error(i->getType(type), p) || type == SymbolRef::ST_External)
          continue;
        if (error(i->getName(name), p)) continue;
        InputSymbol is;
        is.Identifier = C.getName(name);
        is.Location.Type = AtomLocator::ALT_Object;
        is.Location.ObjectInfo.Obj = o;
        ist.add(is);
      }
    }
  }

  virtual FileReader *createReader(const AtomLocator &al) {
    return 0;
  }

  virtual InputSymbolTable::iterator lookupSymbol( InputSymbolTable &ist
                                                 , Name n) {
    return ist.end();
  }
};

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.

  cl::ParseCommandLineOptions(argc, argv, "LLVM Object Link Editor\n");
  ToolName = argv[0];

  Context C;
  TargetInfoMicrosoft ti(C);
  InputSymbolTable ist(ti);

  for (std::vector<std::string>::const_iterator i = InputFilenames.begin(),
                                                e = InputFilenames.end();
                                                i != e; ++i) {
    ti.readExternalSymbols(ist, *i);
  }

  for (InputSymbolTable::iterator i = ist.begin(), e = ist.end(); i != e; ++i) {
    outs() << i->Identifier.str() << ": Type (";

    switch (i->Location.Type) {
    case AtomLocator::ALT_None:
      outs() << "ALT_None";
      break;
    case AtomLocator::ALT_Object:
      outs() << "ALT_Object";
      break;
    case AtomLocator::ALT_ArchiveMember:
      outs() << "ALT_ArchiveMember";
      break;
    case AtomLocator::ALT_LibraryImport:
      outs() << "ALT_LibraryImport";
      break;
    }

    outs() << ")\n";
  }

  return 0;
}
