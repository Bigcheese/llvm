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

#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"
#include <algorithm>
#include <string>
#include <vector>
using namespace llvm;
using namespace object;

static cl::list<std::string>
  InputFilenames(cl::Positional,
                 cl::desc("<input object files>"),
                 cl::ZeroOrMore);

static StringRef ToolName;

static bool error(error_code ec) {
  if (!ec) return false;

  outs() << ToolName << ": error reading file: " << ec.message() << ".\n";
  outs().flush();
  return true;
}

class Link;

class Atom {
public:
  virtual ~Atom() {}

  bool Defined;
  StringRef Contents;
  StringRef Name;
  std::vector<Link> Links;
};

class Link {
public:
  std::vector<Atom*> Operands;
  enum {
    LT_Reloc,
    LT_FollowsFromConstraint
  } Type;
  union {
    uint32_t ConstraintDistance;
    uint64_t RelocInfo;
  };
  virtual ~Link() {}
};

class Module {
public:
  OwningPtr<Binary> binary;
  std::vector<Atom> Atoms;
};

static bool SortOffset(const SymbolRef &a, const SymbolRef &b) {
  uint64_t off_a, off_b;
  if (error(a.getOffset(off_a))) return false;
  if (error(b.getOffset(off_b))) return false;
  return off_a < off_b;
}

static Module *getCOFFModule(StringRef file) {
  Module *m = new Module;
  if (!sys::fs::exists(file)) {
    errs() << ToolName << ": '" << file << "': " << "No such file\n";
    return m;
  }

  // Attempt to open the binary.
  if (error_code ec = createBinary(file, m->binary)) {
    errs() << ToolName << ": '" << file << "': " << ec.message() << ".\n";
    return m;
  }

  if (COFFObjectFile *o = dyn_cast<COFFObjectFile>(m->binary.get())) {
    error_code ec;
    for (section_iterator i = o->begin_sections(),
                          e = o->end_sections();
                          i != e; i.increment(ec)) {
      if (error(ec)) break;
      // Gather up the symbols this section defines.
      std::vector<SymbolRef> Symbols;
      for (symbol_iterator si = o->begin_symbols(),
                           se = o->end_symbols();
                           si != se; si.increment(ec)) {
        bool contains;
        if (!error(i->containsSymbol(*si, contains)) && contains) {
          Symbols.push_back(*si);
        }
      }
      // Sort by address.
      std::stable_sort(Symbols.begin(), Symbols.end(), SortOffset);
      // Create atoms by address range.
      if (Symbols.size() <= 1) {
        Atom a;
        if (error(i->getContents(a.Contents))) return m;
        a.Defined = true;
        if (error(i->getName(a.Name))) return m;
        m->Atoms.push_back(a);
      } else {
        StringRef Bytes;
        if (error(i->getContents(Bytes))) break;
        uint64_t Size;
        uint64_t Index;
        uint64_t SectSize;
        if (error(i->getSize(SectSize))) break;

        for (unsigned si = 0, se = Symbols.size(); si != se; ++si) {
          uint64_t Start;
          uint64_t End;
          Symbols[si].getOffset(Start);
          // The end is either the size of the section or the beginning of the next
          // symbol.
          if (si == se - 1)
            End = SectSize;
          else {
            Symbols[si + 1].getOffset(End);
            // Make sure this symbol takes up space.
            if (End != Start)
              --End;
            else {
              // Create empty atom?
              StringRef name;
              Symbols[si].getName(name);
              errs() << name << " Empty atom!\n";
            }
          }
          Atom a;
          a.Contents = Bytes.substr(Start, Start - End);
          a.Defined = true;
          if (error(Symbols[si].getName(a.Name))) return m;
          m->Atoms.push_back(a);
        }
      }
    }
  } else {
    errs() << ToolName << ": '" << file << "': " << "Unrecognized file type.\n";
  }
  return m;
}

class AtomRef {
public:
  AtomRef()
    : Priority(0)
    , Instance(0) {
  }

  std::string Name;
  uint32_t Priority;
  std::string Path;
  Atom *Instance;

  bool operator <(const AtomRef &other) const {
    if (Name < other.Name)
      return true;
    else if (Name == other.Name && Priority < other.Priority)
      return true;
    return false;
  }
};

static std::vector<AtomRef> Symbtab;

static void ProcessInput(StringRef file, uint32_t priority) {
  if (!sys::fs::exists(file)) {
    errs() << ToolName << ": '" << file << "': " << "No such file\n";
    return;
  }

  // Attempt to open the binary.
  OwningPtr<Binary> binary;
  if (error_code ec = createBinary(file, binary)) {
    errs() << ToolName << ": '" << file << "': " << ec.message() << ".\n";
    return;
  }

  if (Archive *a = dyn_cast<Archive>(binary.get())) {
    uint32_t obj_priority = 0;
    error_code ec;
    for (Archive::symbol_iterator i = a->begin_symbols(),
                                  e = a->end_symbols(); i != e; ++i) {
      StringRef name;
      Archive::child_iterator child;
      StringRef child_name;
      if (error(i->getName(name))) continue;
      if (error(i->getMember(child))) continue;
      if (error(child->getName(child_name))) continue;
      AtomRef ar;
      ar.Name = name;
      ar.Path = a->getFileName();
      ar.Path += '/';
      ar.Path += child_name;
      ar.Priority = (priority << 16) | obj_priority++;
      Symbtab.push_back(ar);
    }
  } else if (ObjectFile *o = dyn_cast<ObjectFile>(binary.get())) {
    error_code ec;
    for (symbol_iterator i = o->begin_symbols(),
                         e = o->end_symbols(); i != e; i.increment(ec)) {
      if (error(ec)) return;
      bool global;
      StringRef name;
      SymbolRef::Type type;
      if (error(i->isGlobal(global)) || !global) continue;
      if (error(i->getType(type)) || type == SymbolRef::ST_External) continue;
      if (error(i->getName(name))) continue;
      AtomRef ar;
      ar.Name = name;
      ar.Path = o->getFileName();
      ar.Priority = priority << 16;
      Symbtab.push_back(ar);
    }
  } else {
    errs() << ToolName << ": '" << file << "': " << "Unrecognized file type.\n";
  }
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.

  cl::ParseCommandLineOptions(argc, argv, "LLVM Object Link Editor\n");
  ToolName = argv[0];

  // Gather symbol table entries.
  for (int i = 0, e = InputFilenames.size(); i != e; ++i) {
    ProcessInput(InputFilenames[i], i);
  }

  // Sort symbol table by name and then priority.
  std::sort(Symbtab.begin(), Symbtab.end());

  // Print it!
  for (std::vector<AtomRef>::const_iterator i = Symbtab.begin(),
                                            e = Symbtab.end(); i != e; ++i) {
    outs() << i->Name << " -> [" << i->Priority << "]" << i->Path << "\n";
  }

  Module *m = getCOFFModule(InputFilenames[0]);
  for (std::vector<Atom>::const_iterator i = m->Atoms.begin(),
                                         e = m->Atoms.end(); i != e; ++i) {
    outs() << i->Name << "\n";
  }
  delete m;

  return 0;
}
