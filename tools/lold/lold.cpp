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
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/Context.h"
#include "llvm/Object/Module.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"
#include <algorithm>
#include <map>
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

static bool SortOffset(const SymbolRef &a, const SymbolRef &b) {
  uint64_t off_a, off_b;
  if (error(a.getOffset(off_a))) return false;
  if (error(b.getOffset(off_b))) return false;
  return off_a < off_b;
}

static bool RelocAddressLess(RelocationRef a, RelocationRef b) {
  uint64_t a_addr, b_addr;
  if (error(a.getAddress(a_addr))) return false;
  if (error(b.getAddress(b_addr))) return false;
  return a_addr < b_addr;
}

typedef std::map<SectionRef, std::vector<SymbolRef> > SectionSymbolMap_t;
typedef std::map<SymbolRef, Atom*> SymbolAtomMap_t;

static error_code buildSectionSymbolAndAtomMap(Module &m,
                                               const ObjectFile *o,
                                               SectionSymbolMap_t &symb,
                                               SymbolAtomMap_t &atom) {
  error_code ec;
  for (symbol_iterator i = o->begin_symbols(),
                       e = o->end_symbols();
                       i != e; i.increment(ec)) {
    if (ec) return ec;
    // Create a null atom for each symbol and get the section it's in.
    StringRef name;
    section_iterator sec = o->end_sections();
    if (error_code ec = i->getName(name)) return ec;
    if (error_code ec = i->getSection(sec)) return ec;
    Name n = m.getContext().getName(name);
    atom[*i] = m.getOrCreateAtom(n);
    if (sec != o->end_sections())
      symb[*sec].push_back(*i);
  }
  return object_error::success;
}

static error_code getModule(StringRef file,
                            OwningPtr<Module> &result,
                            Context &C) {
  error_code ec;
  bool exists;
  if (error_code ec = sys::fs::exists(file, exists)) return ec;
  if (!exists) return make_error_code(errc::no_such_file_or_directory);

  // Attempt to open the binary.
  OwningPtr<Binary> binary;
  if (error_code ec = createBinary(file, binary)) return ec;

  if (ObjectFile *o = dyn_cast<ObjectFile>(binary.get())) {
    binary.take();
    error_code ec;
    OwningPtr<Module> m;
    {
      OwningPtr<ObjectFile> obj(o);
      m.reset(new Module(C, obj, ec));
    }
    SectionSymbolMap_t SectionSymbols;
    SymbolAtomMap_t SymbolAtoms;
    if (error_code ec = buildSectionSymbolAndAtomMap(*m, o,
                                                     SectionSymbols,
                                                     SymbolAtoms))
      return ec;
    for (section_iterator i = o->begin_sections(),
                          e = o->end_sections();
                          i != e; i.increment(ec)) {
      if (ec) return ec;
      // Gather up the symbols this section defines.
      std::vector<SymbolRef> &Symbols = SectionSymbols[*i];

      // Sort symbols by address.
      std::stable_sort(Symbols.begin(), Symbols.end(), SortOffset);

      // Make a list of all the relocations for this section.
      std::vector<RelocationRef> Rels;
      for (relocation_iterator ri = i->begin_relocations(),
                               re = i->end_relocations();
                               ri != re; ri.increment(ec)) {
        if (ec) return ec;
        Rels.push_back(*ri);
      }
      // Sort relocations by address.
      std::stable_sort(Rels.begin(), Rels.end(), RelocAddressLess);

      // Create atoms by address range.
      if (Symbols.empty()) {
        StringRef name;
        if (error_code ec = i->getName(name)) return ec;
        Atom *a = m->getOrCreateAtom(C.getName(name));
        if (error_code ec = i->getContents(a->Contents)) return ec;
        a->Defined = true;
      } else {
        StringRef Bytes;
        if (error_code ec = i->getContents(Bytes)) return ec;
        uint64_t SectSize;
        if (error_code ec = i->getSize(SectSize)) return ec;

        std::vector<RelocationRef>::const_iterator rel_cur = Rels.begin();
        std::vector<RelocationRef>::const_iterator rel_end = Rels.end();
        Atom *a = 0;
        for (unsigned si = 0, se = Symbols.size(); si != se; ++si) {
          uint64_t Start;
          uint64_t End;
          Symbols[si].getOffset(Start);
          // The end is either the size of the section or the beginning of the
          // next symbol.
          if (si == se - 1)
            End = SectSize;
          else {
            Symbols[si + 1].getOffset(End);
            // Make sure this symbol takes up space.
            if (End != Start)
              --End;
          }
          Atom *prev = a;
          a = SymbolAtoms[Symbols[si]];
          if (End == Start) // Empty
            a->Contents = StringRef();
          else
            a->Contents = Bytes.substr(Start, Start - End);
          a->Defined = true;
          if (prev && si != 0) {
            Link l;
            l.Type = Link::LT_LocationOffsetConstraint;
            l.ConstraintDistance = prev->Contents.size();
            l.Operands.push_back(prev);
            a->Links.push_back(l);
          }
          // Add relocations.
          while (rel_cur != rel_end) {
            uint64_t addr;
            if (error_code ec = rel_cur->getAddress(addr)) return ec;
            if (addr > End) break;
            SymbolRef symb;
            if (error_code ec = rel_cur->getSymbol(symb)) return ec;
            SymbolAtomMap_t::const_iterator atom = SymbolAtoms.find(symb);
            if (atom != SymbolAtoms.end()) {
              Link l;
              l.Type = Link::LT_Relocation;
              l.Operands.push_back(atom->second);
              a->Links.push_back(l);
            }
            ++rel_cur;
          }
        }
      }
    }
    // The module was atomized successfully, give it to the result.
    result.swap(m);
    return object_error::success;
  } else {
    return object_error::invalid_file_type;
  }
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

  Context C;
  OwningPtr<Module> m;
  if (!error(getModule(InputFilenames[0], m, C))) {
    for (Module::atom_iterator i = m->atom_begin(),
                               e = m->atom_end(); i != e; ++i) {
      outs() << "atom" << i << " [label=\"" << i->_Name.str() << "\"]\n";
      for (std::vector<Link>::const_iterator li = i->Links.begin(),
                                             le = i->Links.end();
                                             li != le; ++li) {
        outs() << "atom" << i << " -> {";
        for (Link::operand_iterator oi = li->Operands.begin(),
                                    oe = li->Operands.end();
                                    oi != oe; ++oi) {
          outs() << "atom" << *oi << " ";
        }
        outs() << "} [label=\"";
        switch (li->Type) {
        case Link::LT_LocationOffsetConstraint:
          outs() << "LT_LocationOffsetConstraint ("
                 << li->ConstraintDistance << ")";
          break;
        case Link::LT_Relocation:
          outs() << "LT_Relocation";
          break;
        }
        outs() << "\"]\n";
      }
    }
  }

  return 0;
}
