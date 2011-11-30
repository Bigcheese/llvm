//===- Module.cpp - Object File Module --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Twine.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/Context.h"
#include "llvm/Object/Module.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace object;

Module::Module(Context &c, OwningPtr<ObjectFile> &from, error_code &ec)
 : C(c) {
  // Not a swap because we want to null out from.
  Represents.reset(from.take());
}

Module::~Module() {}

static bool SortOffset(const SymbolRef &a, const SymbolRef &b) {
  uint64_t off_a, off_b;
  if (a.getFileOffset(off_a)) return false;
  if (b.getFileOffset(off_b)) return false;
  return off_a < off_b;
}

static bool RelocAddressLess(RelocationRef a, RelocationRef b) {
  uint64_t a_addr, b_addr;
  if (a.getAddress(a_addr)) return false;
  if (b.getAddress(b_addr)) return false;
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
    if (const COFFObjectFile *coff = dyn_cast<const COFFObjectFile>(o)) {
      const coff_symbol *symb = coff->toSymb(i->getRawDataRefImpl());
      bool defined = symb->SectionNumber > COFF::IMAGE_SYM_UNDEFINED;
      bool external = symb->StorageClass == COFF::IMAGE_SYM_CLASS_EXTERNAL;
      Name n = m.getContext().getName(name);
      Atom *a;
      if (defined && external)
        a = atom[*i] = m.getOrCreateAtom<PhysicalAtom>(n);
      else if (defined)
        a = atom[*i] = m.createAtom<PhysicalAtom>(n);
      else if (external)
        a = atom[*i] = m.getOrCreateAtom<Atom>(n);
      else
        a = atom[*i] = m.createAtom<Atom>(n);
      a->Scope = symb->StorageClass == COFF::IMAGE_SYM_CLASS_EXTERNAL
                   ? Atom::ScopeGlobal
                   : Atom::ScopeTranslationUnit;

      a->Definition = symb->SectionNumber == COFF::IMAGE_SYM_ABSOLUTE
                        ? Atom::DefinitionAbsolute
                        : Atom::DefinitionRegular;

    } else
      atom[*i] = m.getOrCreateAtom<Atom>(m.getContext().getName(name));
    if (sec != o->end_sections())
      symb[*sec].push_back(*i);
  }
  return object_error::success;
}

error_code Module::mergeObject(ObjectFile *o) {
  ObjName = C.getName(o->getFileName());

  error_code ec;
  SectionSymbolMap_t SectionSymbols;
  SymbolAtomMap_t SymbolAtoms;
  if (error_code ec = buildSectionSymbolAndAtomMap(*this, o,
                                                    SectionSymbols,
                                                    SymbolAtoms))
    return ec;
  for (section_iterator i = o->begin_sections(),
                        e = o->end_sections();
                        i != e; i.increment(ec)) {
    if (ec) return ec;
    PhysicalAtom::Section *section = new PhysicalAtom::Section;
    {
      StringRef name;
      bool isData;
      bool isCode;
      bool isBSS;
      if (error_code ec = i->getName(name)) return ec;
      if (error_code ec = i->isData(isData)) return ec;
      if (error_code ec = i->isText(isCode)) return ec;
      if (error_code ec = i->isBSS(isBSS)) return ec;
      section->Identifier = C.getName(name);
      if (isData)
        section->Type = PhysicalAtom::Section::InitializedData;
      else if (isCode)
        section->Type = PhysicalAtom::Section::Code;
      else if (isBSS)
        section->Type = PhysicalAtom::Section::UninitializedData;
      else
        section->Type = PhysicalAtom::Section::Unclasified;
    }


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
      PhysicalAtom *a =
        cast<PhysicalAtom>(getOrCreateAtom<Atom>(C.getName(name)));
      StringRef Contents;
      if (error_code ec = i->getContents(Contents)) return ec;
      a->setContents(Contents);
      a->setInputSection(section);
    } else {
      StringRef Bytes;
      if (error_code ec = i->getContents(Bytes)) return ec;
      uint64_t SectSize;
      if (error_code ec = i->getSize(SectSize)) return ec;

      std::vector<RelocationRef>::const_iterator rel_cur = Rels.begin();
      std::vector<RelocationRef>::const_iterator rel_end = Rels.end();
      PhysicalAtom *a = 0;
      for (unsigned si = 0, se = Symbols.size(); si != se; ++si) {
        uint64_t Start;
        uint64_t End;
        Symbols[si].getAddress(Start);
        // The end is either the size of the section or the beginning of the
        // next symbol.
        if (si == se - 1)
          End = SectSize;
        else {
          Symbols[si + 1].getAddress(End);
        }
        PhysicalAtom *prev = a;
        a = cast<PhysicalAtom>(SymbolAtoms[Symbols[si]]);
        a->setInputSection(section);
        if (End == Start) // Empty
          a->setContents(StringRef());
        else
          a->setContents(Bytes.substr(Start, End - Start));
        if (prev && si != 0) {
          Link l;
          l.Type = Link::LT_LocationOffsetConstraint;
          l.ConstraintDistance = prev->getPhysicalSize();
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
            if (error_code ec = rel_cur->getAddress(l.RelocAddr)) return ec;
            // Adjust for atom relative.
            l.RelocAddr -= Start;
            if (error_code ec = rel_cur->getType(l.RelocType)) return ec;
            l.Operands.push_back(atom->second);
            a->Links.push_back(l);
          }
          ++rel_cur;
        }
      }
    }
  }
  return object_error::success;
}

void Module::printGraph(raw_ostream &o) {
  o << "subgraph \"cluster_" << ObjName.str() << "\" {\n";
  for (atom_iterator i = atom_begin(), e = atom_end(); i != e; ++i) {
    o << "atom" << i << " [label=\"" << i->Identifier.str() << "\"";
    if (isa<PhysicalAtom>(i))
      o << " shape=box ";
    if (i->Scope == Atom::ScopeGlobal)
      o << " color=green ";
    o << "];\n";
  }
  o << "}\n";
  for (atom_iterator i = atom_begin(), e = atom_end(); i != e; ++i) {
    for (Atom::LinkList_t::iterator li = i->Links.begin(),
                                    le = i->Links.end();
                                    li != le; ++li) {
      o << "atom" << i << " -> {";
      for (Link::operand_iterator oi = li->Operands.begin(),
                                  oe = li->Operands.end();
                                  oi != oe; ++oi) {
          o << "atom" << *oi << " ";
      }
      o << "} [label=\"";
      switch (li->Type) {
      case Link::LT_LocationOffsetConstraint:
        o << "LT_LocationOffsetConstraint ("
          << li->ConstraintDistance << ")";
        break;
      case Link::LT_Relocation:
        o << "LT_Relocation";
        break;
      case Link::LT_ResolvedTo:
        o << "LT_ResolvedTo";
        break;
      }
      o << "\"];\n";
    }
  }
}

void Module::mergeModule(Module *m) {
  Atoms.splice(Atoms.end(), m->Atoms);
}

Atom *Module::replaceAllUsesWith(Atom *a, Atom *New) {
  for (atom_iterator i = Atoms.begin(), e = Atoms.end(); i != e; ++i) {
    for (Atom::LinkList_t::iterator li = i->Links.begin(),
                                    le = i->Links.end(); li != le; ++li) {
      for (Link::operand_iterator oi = li->Operands.begin(),
                                  oe = li->Operands.end(); oi != oe; ++oi) {
        if ((*oi) == a)
          *oi = New;
      }
    }
  }
  return a;
}
