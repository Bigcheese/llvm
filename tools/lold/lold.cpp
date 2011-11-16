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
#include "llvm/Object/Context.h"
#include "llvm/Object/Module.h"
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

static cl::list<std::string>
  InputFilenames(cl::Positional,
                 cl::desc("<input object files>"),
                 cl::ZeroOrMore);

static cl::opt<bool>
  PrintDOT("dot");

static cl::opt<bool>
  PrintGEXF("gexf");

static cl::opt<bool>
  PrintSymtab("symtab");

static cl::opt<bool>
  PrintLayout("layout");

static StringRef ToolName;

static bool error(error_code ec) {
  if (!ec) return false;

  errs() << ToolName << ": error reading file: " << ec.message() << ".\n";
  errs().flush();
  return true;
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

  } else {
    return object_error::invalid_file_type;
  }
}

class AtomRef {
public:
  AtomRef()
    : Priority(0)
    , Instance(0)
    , Obj(0)
    , Arch(0) {
  }

  Name Name;
  uint32_t Priority;
  Atom *Instance;
  ObjectFile *Obj;
  Archive *Arch;
  Archive::child_iterator Member;

  bool operator <(const AtomRef &other) const {
    if (Name < other.Name)
      return true;
    else if (Name == other.Name && Priority < other.Priority)
      return true;
    return false;
  }
};

typedef std::vector<AtomRef> Symbtab_t;
static Symbtab_t Symbtab;

static void ProcessInput(StringRef file, uint32_t priority, Context &C) {
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
    binary.take();
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
      ar.Arch = a;
      ar.Member = child;
      ar.Name = C.getName(name);
      ar.Priority = (priority << 16) | obj_priority++;
      Symbtab.push_back(ar);
    }
  } else if (ObjectFile *o = dyn_cast<ObjectFile>(binary.get())) {
    binary.take();
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
      ar.Obj = o;
      ar.Name = C.getName(name);
      ar.Priority = priority << 16;
      Symbtab.push_back(ar);
    }
  } else {
    errs() << ToolName << ": '" << file << "': " << "Unrecognized file type.\n";
  }
}

struct FindAtomRefString {
  bool operator ()(const AtomRef &a, Name n) {
    return a.Name < n;
  }
};

static std::string xmlencode(StringRef s) {
  std::stringstream m;
  for (StringRef::const_iterator i = s.begin(), e = s.end(); i != e; ++i) {
    switch (*i) {
      case '<':
        m << "&lt;";
        break;
      case '>':
        m << "&gt;";
        break;
      case '&':
        m << "&amp;";
        break;
      case '\'':
        m << "&apos;";
        break;
      case '"':
        m << "&quot;";
        break;
      default:
        m << *i;
    }
  }
  return m.str();
}

struct coff_import_header {
  support::ulittle16_t Sig1;
  support::ulittle16_t Sig2;
  support::ulittle16_t Version;
  support::ulittle16_t Machine;
  support::ulittle32_t TimeDateStamp;
  support::ulittle32_t SizeOfData;
  support::ulittle16_t OrdinalHint;
  support::ulittle16_t TypeInfo;
};

class DLLImportData : public Atom {
  typedef std::vector<COFF::ImportDirectoryTableEntry> IDT_t;
  typedef std::vector<COFF::ImportLookupTableEntry32> ILT_t;
  typedef std::vector<std::pair<uint16_t, Name> > HintNameTable_t;
  typedef std::vector<Name> NameTable_t;

  IDT_t IDT;
  ILT_t ILT;
  HintNameTable_t HintNameTable;
  uint32_t currentoffset;
  NameTable_t NameTable;

  std::vector<char> Data;
  uint32_t IATStart;
  uint32_t IDTStart;
  uint32_t ILTStart;
  uint32_t HintNameTableStart;
  uint32_t NameTableStart;

  friend class Module;

protected:
  DLLImportData()
    : currentoffset(0) {}

public:
  COFF::DataDirectory getIAT() const {
    COFF::DataDirectory dd;
    dd.RelativeVirtualAddress = RVA + IATStart;
    dd.Size = IDTStart - IATStart;
    return dd;
  }

  COFF::DataDirectory getIDT() const {
    COFF::DataDirectory dd;
    dd.RelativeVirtualAddress = RVA + IDTStart;
    dd.Size = ILTStart - IDTStart;
    return dd;
  }

  uint32_t addImport(Atom *a) {
    assert(a->Type == Atom::AT_Import && "a must be an AT_Import!");

    // Only support one imported library for now.
    if (IDT.empty()) {
      COFF::ImportDirectoryTableEntry IDTe;
      IDTe.ImportLookupTableRVA  = 0;
      IDTe.TimeDateStamp         = 0;
      IDTe.ForwarderChain        = 0;
      IDTe.NameRVA               = NameTable.size();
      IDTe.ImportAddressTableRVA = 0;
      IDT.push_back(IDTe);
      NameTable.push_back(a->ImportFrom);
    }

    COFF::ImportLookupTableEntry32 ILTe;
    ILTe.setHintNameRVA(currentoffset);
    std::size_t len = a->_Name.str().size() + 1;
    if (len & 1)
        ++len;
    currentoffset += 2 + len;
    HintNameTable.push_back(std::make_pair(0, a->_Name));
    ILT.push_back(ILTe);

    return (ILT.size() - 1) * 4;
  }

  uint32_t layout() {
    uint32_t total = 0;

    IATStart = 0;
    total += ILT.size() * sizeof(COFF::ImportLookupTableEntry32);
    IDTStart = total;
    total += IDT.size() * sizeof(COFF::ImportDirectoryTableEntry);
    ILTStart = total;
    total += ILT.size() * sizeof(COFF::ImportLookupTableEntry32);

    HintNameTableStart = total;
    for (HintNameTable_t::const_iterator
           i = HintNameTable.begin(), e = HintNameTable.end(); i != e; ++i) {
      std::size_t len = i->second.str().size() + 1;
      if (len & 1)
        ++len;
      total += 2 + len;
    }

    NameTableStart = total;
    for (NameTable_t::const_iterator i = NameTable.begin(),
                                     e = NameTable.end(); i != e; ++i) {
      total += i->str().size() + 1;
    }

    return total;
  }

  void finalize(uint32_t SectionBaseRVA) {
    // Add null final entries.
    COFF::ImportDirectoryTableEntry IDTe;
    std::memset(&IDTe, 0, sizeof(IDTe));
    IDT.push_back(IDTe);

    COFF::ImportLookupTableEntry32 ILTe;
    std::memset(&ILTe, 0, sizeof(ILTe));
    ILT.push_back(ILTe);

    // Figure out where everything goes and reserve the size.
    Data.resize(layout());

    // Fixup references.
    for (ILT_t::iterator i = ILT.begin(), e = ILT.end() - 1; i != e; ++i) {
      i->setHintNameRVA(SectionBaseRVA
                        + HintNameTableStart
                        + i->getHintNameRVA());
    }

    for (IDT_t::iterator i = IDT.begin(), e = IDT.end() - 1; i != e; ++i) {
      i->ImportLookupTableRVA = SectionBaseRVA + ILTStart;
      i->NameRVA = SectionBaseRVA + NameTableStart + i->NameRVA;
      i->ImportAddressTableRVA = SectionBaseRVA + IATStart;
    }

    // Write out the data to Data and set the section contents.
    char *output = &Data.front();
    // IAT.
    for (ILT_t::iterator i = ILT.begin(), e = ILT.end(); i != e; ++i) {
      endian::write_le<uint32_t, unaligned>(output, i->data);
      output += sizeof(uint32_t);
    }
    // IDT.
    for (IDT_t::iterator i = IDT.begin(), e = IDT.end(); i != e; ++i) {
      endian::write_le<uint32_t, unaligned>(output, i->ImportLookupTableRVA);
      output += sizeof(uint32_t);
      endian::write_le<uint32_t, unaligned>(output, i->TimeDateStamp);
      output += sizeof(uint32_t);
      endian::write_le<uint32_t, unaligned>(output, i->ForwarderChain);
      output += sizeof(uint32_t);
      endian::write_le<uint32_t, unaligned>(output, i->NameRVA);
      output += sizeof(uint32_t);
      endian::write_le<uint32_t, unaligned>(output, i->ImportAddressTableRVA);
      output += sizeof(uint32_t);
    }
    // ILT.
    for (ILT_t::iterator i = ILT.begin(), e = ILT.end(); i != e; ++i) {
      endian::write_le<uint32_t, unaligned>(output, i->data);
      output += sizeof(uint32_t);
    }
    // Hint/Name Table.
    for (HintNameTable_t::const_iterator
           i = HintNameTable.begin(), e = HintNameTable.end(); i != e; ++i) {
      endian::write_le<uint16_t, aligned>(output, i->first);
      output += sizeof(uint16_t);
      std::memcpy(output, i->second.str().data(), i->second.str().size());
      output += i->second.str().size();
      *output++ = 0;
      if ((i->second.str().size() + 1) & 1)
        *output++ = 0;
    }
    // Name table.
    for (NameTable_t::const_iterator i = NameTable.begin(),
                                     e = NameTable.end(); i != e; ++i) {
      std::memcpy(output, i->str().data(), i->str().size());
      output += i->str().size();
      *output++ = 0;
    }

    // Set contents.
    Contents = StringRef(&Data.front(), Data.size());

    // Clear out the data we generated from.
    IDT.clear();
    ILT.clear();
    HintNameTable.clear();
    currentoffset = 0;
    NameTable.clear();
  }

  void dump() {

  }
};

Atom *getRoot(Atom *a) {
  for (Atom::LinkList_t::const_iterator li = a->Links.begin(),
                                        le = a->Links.end(); li != le; ++li) {
    if (li->Type == Link::LT_LocationOffsetConstraint) {
      return getRoot(li->Operands[0]);
    }
  }
  return a;
}

uint64_t getDistanceToRoot(Atom *a) {
  uint64_t total = 0;
  for (Atom::LinkList_t::const_iterator li = a->Links.begin(),
                                        le = a->Links.end(); li != le; ++li) {
    if (li->Type == Link::LT_LocationOffsetConstraint) {
      total += getDistanceToRoot(li->Operands[0]) + li->ConstraintDistance;
    }
  }
  return total;
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  Context C;

  cl::ParseCommandLineOptions(argc, argv, "LLVM Object Link Editor\n");
  ToolName = argv[0];

  // Gather symbol table entries.
  for (int i = 0, e = InputFilenames.size(); i != e; ++i) {
    ProcessInput(InputFilenames[i], i, C);
  }

  // Sort symbol table by name and then priority.
  std::sort(Symbtab.begin(), Symbtab.end());

  if (PrintSymtab) {
    // Print it!
    for (std::vector<AtomRef>::const_iterator i = Symbtab.begin(),
                                              e = Symbtab.end(); i != e; ++i) {
      outs() << i->Name.str() << " -> [" << i->Priority << "]";
      if (i->Obj)
        outs() << i->Obj->getFileName();
      else if (i->Arch) {
        StringRef name;
        i->Member->getName(name);
        outs() << i->Arch->getFileName() << "/" << name;
      }
      outs() << "\n";
    }
  }

  std::vector<Atom*> UndefinedExternals;
  std::vector<Module*> Modules;
  std::map<ObjectFile*, Module*> ModMap;
  std::map<Archive::child_iterator, ObjectFile*> ChildMap;
  typedef std::map<Name, Module*> ImportMap_t;
  ImportMap_t ImportMap;

  // Create empty module for output.
  OwningPtr<Module> output(new Module(C));
  Modules.push_back(output.get());
  // Add starting atom.
  // Atom *start = output->getOrCreateAtom<Atom>(C.getName("_mainCRTStartup"));
  Atom *start = output->getOrCreateAtom<Atom>(C.getName("_main"));
  start->External = true;
  UndefinedExternals.push_back(start);

  while (!UndefinedExternals.empty()) {
    Atom *a = *--UndefinedExternals.end();
    UndefinedExternals.pop_back();
    // Find this symbol.
    Symbtab_t::iterator i = std::lower_bound( Symbtab.begin()
                                            , Symbtab.end()
                                            , a->_Name
                                            , FindAtomRefString());
    if (i == Symbtab.end()) {
      // errs() << "Ohnoes, couldn't find symbol! " << a->_Name.str() << "\n";
      continue;
    }

    errs() << i->Name.str() << " -> [" << i->Priority << "]\n";

    if (!i->Instance) {
      ObjectFile *o = 0;
      if (i->Obj)
        o = i->Obj;
      else if (i->Arch) {
        if (ChildMap.find(i->Member) != ChildMap.end()) {
          o = i->Obj = ChildMap[i->Member];
        } else {
          OwningPtr<Binary> b;
          if (error_code ec = i->Member->getAsBinary(b)) {
            if (ec == object_error::invalid_file_type) {
              // Attempt to open it as an import entry.
              OwningPtr<MemoryBuffer> data(i->Member->getBuffer());
              const coff_import_header *cih =
                reinterpret_cast<const coff_import_header*>(
                  data->getBufferStart());
              if (cih->Sig1 != 0 || cih->Sig2 != 0xffff) {
                error(ec);
                StringRef name;
                if (!i->Member->getName(name))
                  errs() << name << "\n";
                continue;
              }
              // We have a valid import entry. Get the module for it and add it.
              const char *symname = data->getBufferStart()
                                    + sizeof(coff_import_header);
              const char *dllname = symname + strlen(symname) + 1;
              Name DLLName = C.getName(dllname);
              ImportMap_t::const_iterator imci = ImportMap.find(DLLName);
              if (imci == ImportMap.end()) {
                Module *m = new Module(C);
                m->ObjName = DLLName;
                Modules.push_back(m);
                imci = ImportMap.insert(std::make_pair(DLLName, m)).first;
              }
              Module *m = imci->second;
              // Trim leading and trailing mangling off symname.
              StringRef SymbolName(symname);
              SymbolName = SymbolName.substr(1);
              SymbolName = SymbolName.substr(0, SymbolName.rfind('@'));
              Atom *a = m->getOrCreateAtom<Atom>(C.getName(SymbolName));
              a->External = true;
              a->Defined = true;
              a->Type = Atom::AT_Import;
              a->ImportFrom = DLLName;
              i->Instance = a;
              goto dolink;
            } else {
              error(ec);
              StringRef name;
              if (!i->Member->getName(name))
                errs() << name << "\n";
              continue;
            }
          }
          o = dyn_cast<ObjectFile>(b.get());
          if (o) {
            b.take();
            i->Obj = o;
            ChildMap[i->Member] = o;
          }
        }
      }

      if (o) {
        if (ModMap.find(o) != ModMap.end()) {
          i->Instance = ModMap[o]->getOrCreateAtom<Atom>(i->Name);
        } else {
          Module *m = new Module(C);
          Modules.push_back(m);
          ModMap[o] = m;
          if (error(m->mergeObject(o))) continue;
          i->Instance = m->getOrCreateAtom<Atom>(i->Name);
          // FIXME: This is horribly inefficient, mergeObject should populate
          //        UndefinedExternals.
          for (Module::atom_iterator ai = m->atom_begin(),
                                     ae = m->atom_end(); ai != ae; ++ai) {
            if (ai->External && !ai->Defined)
              UndefinedExternals.push_back(ai);
          }
        }
      } else
        errs() << "Failed to get object to merge.\n";
    }

  dolink:
    Link l;
    l.Type = Link::LT_ResolvedTo;
    l.Operands.push_back(i->Instance);
    a->Links.push_back(l);
  }

  // Set start to what it resolved to.
  start = start->Links[0].Operands[0];

  outs().flush();
  errs().flush();
  if (PrintDOT) {
    outs() << "digraph {\n";
    for (std::size_t i = 0; i < Modules.size(); ++i) {
      Modules[i]->printGraph(outs());
    }
    outs() << "}\n";
  }

  if (PrintGEXF) {
    outs () << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<gexf xmlns=\"http://www.gexf.net/1.2draft\" version=\"1.2\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"http://www.gexf.net/1.2draft http://www.gexf.net/1.2draft/gexf.xsd\">\n"
            << "<graph defaultedgetype=\"directed\">\n"
            << "<attributes class=\"node\">\n"
            << "<attribute id=\"0\" title=\"Module\" type=\"string\"/>\n"
            << "<attribute id=\"1\" title=\"External\" type=\"boolean\"/>\n"
            << "<attribute id=\"0\" title=\"Type\" type=\"string\"/>\n"
            << "</attributes>\n"
            << "<nodes>\n";

    for (std::size_t i = 0; i < Modules.size(); ++i) {
      for (Module::atom_iterator ai = Modules[i]->atom_begin(),
                                 ae = Modules[i]->atom_end(); ai != ae; ++ai) {
        outs() << "<node id=\"" << "atom" << ai
               << "\" label=\"" << xmlencode(ai->_Name.str()) << "\">\n"
               << "<attvalues>\n"
               << "<attvalue for=\"0\" value=\""
               << Modules[i]->ObjName.str() << "\"/>\n"
               << "<attvalue for=\"1\" value=\"" << (ai->External ? "true" : "false") << "\"/>\n"
               << "<attvalue for=\"2\" value=\"";
        switch (ai->Type) {
        case Atom::AT_Code:
          outs() << "code";
          break;
        case Atom::AT_Data:
          outs() << "data";
          break;
        case Atom::AT_Import:
          outs() << "import";
          break;
        default:
          outs() << "unknown";
        }
        outs() << "\"/>\n"
               << "</attvalues>\n"
               << "</node>\n";
      }
    }
    outs () << "</nodes>\n"
            << "<edges>\n";

    int id = 0;
    for (std::size_t i = 0; i < Modules.size(); ++i) {
      for (Module::atom_iterator ai = Modules[i]->atom_begin(),
                                 ae = Modules[i]->atom_end(); ai != ae; ++ai) {
        for (Atom::LinkList_t::iterator li = ai->Links.begin(),
                                        le = ai->Links.end();
                                        li != le; ++li) {
          for (Link::operand_iterator oi = li->Operands.begin(),
                                      oe = li->Operands.end();
                                      oi != oe; ++oi) {
            outs() << "<edge id=\"" << id++ << "\""
                   << " source=\"" << "atom" << ai << "\""
                   << " target=\"" << "atom" << *oi << "\"/>\n";
          }
        }
      }
    }

    outs() << "</edges>\n"
           << "</graph>\n"
           << "</gexf>\n";
  }

  // Stuff it all into the output module.
  for (std::size_t i = 1; i < Modules.size(); ++i) {
    output->mergeModule(Modules[i]);
  }

  // Collapse all LT_ResolvedTo's.
  for (Module::atom_iterator i = output->atom_begin(),
                             e = output->atom_end(); i != e;) {
    if (!i->Defined && i->External) {
      bool erased = false;
      for (Atom::LinkList_t::iterator li = i->Links.begin(),
                                      le = i->Links.end(); li != le; ++li) {
        if (li->Type == Link::LT_ResolvedTo) {
          i = output->erase(output->replaceAllUsesWith(i, li->Operands[0]));
          erased = true;
          break;
        }
      }
      if (!erased)
        ++i;
    } else
      ++i;
  }

  // Allocate space.
  uint32_t StartPE = sizeof(COFF::DOSHeader);
  uint32_t StartSections = StartPE
                           + sizeof(COFF::PEHeader)
                           + (sizeof(COFF::DataDirectory) * 16);
  // For now we arbitrarily have 3 sections.
  uint32_t EndSections = StartSections + sizeof(COFF::section) * 3;
  uint32_t StartCode   = 0x1000;
  uint32_t CurrentCode = StartCode;
  uint32_t StartData   = 0x2000;
  uint32_t CurrentData = StartData;
  uint32_t StartIData  = 0x3000;
  uint32_t EndFile     = 0x4000;

  // Build IAT.
  DLLImportData *did = output->createAtom<DLLImportData>(C.getName(".idata"));
  did->RVA = StartIData;
  for (Module::atom_iterator i = output->atom_begin(),
                             e = output->atom_end(); i != e; ++i) {
    if (i->Type == Atom::AT_Import)
      i->RVA = did->RVA + did->addImport(i);
  }
  did->finalize(did->RVA);

  // Allocate RVAs.
  // Find roots and groups.
  // FIXME: This doesn't handle multiple LocationOffsetConstraints in one atom
  //        at all :(.
  std::vector<Atom*> Roots;
  std::map<Atom*, std::vector<Atom*> > Groups;
  for (Module::atom_iterator i = output->atom_begin(),
                             e = output->atom_end(); i != e; ++i) {
    Atom *a = getRoot(i);
    if (a == i) {
      Roots.push_back(a);
    } else {
      Groups[a].push_back(i);
    }
  }

  // Now we can layout!
  for (std::vector<Atom*>::iterator i = Roots.begin(),
                                    e = Roots.end(); i != e; ++i) {
    uint32_t *current;
    switch ((*i)->Type) {
    case Atom::AT_Code:
      current = &CurrentCode;
      break;
    case Atom::AT_Data:
      current = &CurrentData;
      break;
    default:
      continue;
    }

    // Align current to 16 bytes.
    if (*current & 0xF)
      *current += 0x10 - (*current & 0xF);

    (*i)->RVA = *current;
    *current += (*i)->Contents.size();
    // Setup group.
    for (std::vector<Atom*>::iterator gi = Groups[*i].begin(),
                                      ge = Groups[*i].end(); gi != ge; ++gi) {
      uint64_t dist = getDistanceToRoot(*gi);
      (*gi)->RVA = dist + (*i)->RVA;
      uint64_t end = (*gi)->RVA + (*gi)->Contents.size();
      if (*current < end)
        *current = end;
    }
  }

  for (Module::atom_iterator i = output->atom_begin(),
                             e = output->atom_end(); i != e; ++i) {
     if (PrintLayout) {
      outs() << "Name: " << i->_Name.str()
              << " RVA: " << i->RVA
              << " Size: " << i->Contents.size()
              << "\n";
    }
  }

  // Everything now has an RVA. Write data!
  std::vector<char> out(EndFile);
  char *fout = &out.front();
  COFF::DOSHeader dh;
  std::memset(&dh, 0, sizeof(dh));
  dh.Magic = 0x5A4D; // "MZ"
  dh.AddressOfNewExeHeader = StartPE;
  endian::write_le<uint16_t, aligned>(fout, dh.Magic);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.UsedBytesInTheLastPage);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.FileSizeInPages);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.NumberOfRelocationItems);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.HeaderSizeInParagraphs);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.MinimumExtraParagraphs);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.MaximumExtraParagraphs);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.InitialRelativeSS);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.InitialSP);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.Checksum);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.InitialIP);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.InitialRelativeCS);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.AddressOfRelocationTable);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.OverlayNumber);
  fout += sizeof(uint16_t);
  for (int i = 0; i < 4; ++i) {
    endian::write_le<uint16_t, aligned>(fout, dh.Reserved[i]);
    fout += sizeof(uint16_t);
  }
  endian::write_le<uint16_t, aligned>(fout, dh.OEMid);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, dh.OEMinfo);
  fout += sizeof(uint16_t);
  for (int i = 0; i < 10; ++i) {
    endian::write_le<uint16_t, aligned>(fout, dh.Reserved2[i]);
    fout += sizeof(uint16_t);
  }
  endian::write_le<uint32_t, aligned>(fout, dh.AddressOfNewExeHeader);
  fout += sizeof(uint32_t);

  // PE header.
  COFF::PEHeader ph;
  std::memset(&ph, 0, sizeof(ph));
  ph.Signature = 0x00004550;
  ph.COFFHeader.Machine = COFF::IMAGE_FILE_MACHINE_I386;
  ph.COFFHeader.NumberOfSections = 3;
  ph.COFFHeader.SizeOfOptionalHeader = (sizeof(COFF::PEHeader)
                                        - sizeof(COFF::header)
                                        - 4)
                                       + sizeof(COFF::DataDirectory) * 16;
  ph.COFFHeader.SizeOfOptionalHeader -= 24;
  ph.COFFHeader.Characteristics |= COFF::IMAGE_FILE_EXECUTABLE_IMAGE
                                   | COFF::IMAGE_FILE_32BIT_MACHINE
                                   | COFF::IMAGE_FILE_RELOCS_STRIPPED;
  ph.Magic                       = 0x10b;
  ph.MajorLinkerVersion          = 8;
  ph.SizeOfCode                  = 4096;
  ph.SizeOfInitializedData       = 8192;
  ph.SizeOfUninitializedData     = 0;
  ph.AddressOfEntryPoint         = start->RVA;
  ph.BaseOfCode                  = StartCode;
  ph.BaseOfData                  = StartData;
  ph.ImageBase                   = 0x400000;
  ph.SectionAlignment            = 4096;
  ph.FileAlignment               = 4096;
  ph.MajorOperatingSystemVersion = 5;
  ph.MinorOperatingSystemVersion = 1;
  ph.MajorSubsystemVersion       = 5;
  ph.MinorSubsystemVersion       = 1;
  ph.SizeOfImage                 = EndFile;
  ph.SizeOfHeaders               = 1024;
  ph.Subsystem                   = COFF::IMAGE_SUBSYSTEM_WINDOWS_CUI;
  ph.SizeOfStackReserve          = 0x100000;
  ph.SizeOfStackCommit           = 0x1000;
  ph.SizeOfHeapReserve           = 0x100000;
  ph.SizeOfHeapCommit            = 0x1000;
  ph.NumberOfRvaAndSize          = 16;

  endian::write_le<uint32_t, aligned>(fout, ph.Signature);
  fout += sizeof(uint32_t);
  endian::write_le<uint16_t, aligned>(fout, ph.COFFHeader.Machine);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, ph.COFFHeader.NumberOfSections);
  fout += sizeof(uint16_t);
  endian::write_le<uint32_t, aligned>(fout, ph.COFFHeader.TimeDateStamp);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.COFFHeader.PointerToSymbolTable);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.COFFHeader.NumberOfSymbols);
  fout += sizeof(uint32_t);
  endian::write_le<uint16_t, aligned>(fout, ph.COFFHeader.SizeOfOptionalHeader);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, ph.COFFHeader.Characteristics);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, ph.Magic);
  fout += sizeof(uint16_t);
  endian::write_le<uint8_t, aligned>(fout, ph.MajorLinkerVersion);
  fout += sizeof(uint8_t);
  endian::write_le<uint8_t, aligned>(fout, ph.MinorLinkerVersion);
  fout += sizeof(uint8_t);
  endian::write_le<uint32_t, aligned>(fout, ph.SizeOfCode);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.SizeOfInitializedData);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.SizeOfUninitializedData);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.AddressOfEntryPoint);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.BaseOfCode);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.BaseOfData);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.ImageBase);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.SectionAlignment);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.FileAlignment);
  fout += sizeof(uint32_t);
  endian::write_le<uint16_t, aligned>(fout, ph.MajorOperatingSystemVersion);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, ph.MinorOperatingSystemVersion);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, ph.MajorImageVersion);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, ph.MinorImageVersion);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, ph.MajorSubsystemVersion);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, ph.MinorSubsystemVersion);
  fout += sizeof(uint16_t);
  endian::write_le<uint32_t, aligned>(fout, ph.Win32VersionValue);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.SizeOfImage);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.SizeOfHeaders);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.CheckSum);
  fout += sizeof(uint32_t);
  endian::write_le<uint16_t, aligned>(fout, ph.Subsystem);
  fout += sizeof(uint16_t);
  endian::write_le<uint16_t, aligned>(fout, ph.DLLCharacteristics);
  fout += sizeof(uint16_t);
  endian::write_le<uint32_t, aligned>(fout, ph.SizeOfStackReserve);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.SizeOfStackCommit);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.SizeOfHeapReserve);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.SizeOfHeapCommit);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.LoaderFlags);
  fout += sizeof(uint32_t);
  endian::write_le<uint32_t, aligned>(fout, ph.NumberOfRvaAndSize);
  fout += sizeof(uint32_t);

  std::vector<COFF::DataDirectory> dirs(16);
  std::memset(&dirs.front(), 0, dirs.size() * sizeof(COFF::DataDirectory));
  dirs[1]  = did->getIDT();
  dirs[12] = did->getIAT();
  for (int i = 0; i < 16; ++i) {
    endian::write_le<uint32_t, aligned>(fout, dirs[i].RelativeVirtualAddress);
    fout += sizeof(uint32_t);
    endian::write_le<uint32_t, aligned>(fout, dirs[i].Size);
    fout += sizeof(uint32_t);
  }

  std::vector<COFF::section> secs(3);
  std::memcpy(secs[0].Name, ".text\0\0", 8);
  secs[0].VirtualSize      = 4096;
  secs[0].VirtualAddress   = StartCode;
  secs[0].SizeOfRawData    = 512;
  secs[0].PointerToRawData = StartCode;
  secs[0].Characteristics |= COFF::IMAGE_SCN_CNT_CODE
                             | COFF::IMAGE_SCN_MEM_EXECUTE
                             | COFF::IMAGE_SCN_MEM_READ;

  std::memcpy(secs[1].Name, ".data\0\0", 8);
  secs[1].VirtualSize      = 4096;
  secs[1].VirtualAddress   = StartData;
  secs[1].SizeOfRawData    = 512;
  secs[1].PointerToRawData = StartData;
  secs[1].Characteristics |= COFF::IMAGE_SCN_CNT_INITIALIZED_DATA
                             | COFF::IMAGE_SCN_MEM_WRITE
                             | COFF::IMAGE_SCN_MEM_READ;

  std::memcpy(secs[2].Name, ".idata\0", 8);
  secs[2].VirtualSize      = 4096;
  secs[2].VirtualAddress   = StartIData;
  secs[2].SizeOfRawData    = 512;
  secs[2].PointerToRawData = StartIData;
  secs[2].Characteristics |= COFF::IMAGE_SCN_CNT_INITIALIZED_DATA
                             | COFF::IMAGE_SCN_MEM_READ;

  for (int i = 0; i < 3; ++i) {
    std::memcpy(fout, secs[i].Name, 8);
    fout += 8;
    endian::write_le<uint32_t, unaligned>(fout, secs[i].VirtualSize);
    fout += sizeof(uint32_t);
    endian::write_le<uint32_t, unaligned>(fout, secs[i].VirtualAddress);
    fout += sizeof(uint32_t);
    endian::write_le<uint32_t, unaligned>(fout, secs[i].SizeOfRawData);
    fout += sizeof(uint32_t);
    endian::write_le<uint32_t, unaligned>(fout, secs[i].PointerToRawData);
    fout += sizeof(uint32_t);
    endian::write_le<uint32_t, unaligned>(fout, secs[i].PointerToRelocations);
    fout += sizeof(uint32_t);
    endian::write_le<uint32_t, unaligned>(fout, secs[i].PointerToLineNumbers);
    fout += sizeof(uint32_t);
    endian::write_le<uint16_t, unaligned>(fout, secs[i].NumberOfRelocations);
    fout += sizeof(uint16_t);
    endian::write_le<uint16_t, unaligned>(fout, secs[i].NumberOfLineNumbers);
    fout += sizeof(uint16_t);
    endian::write_le<uint32_t, unaligned>(fout, secs[i].Characteristics);
    fout += sizeof(uint32_t);
  }

  // Now to copy the data!
  did->Type = Atom::AT_Data;
  for (Module::atom_iterator i = output->atom_begin(),
                             e = output->atom_end(); i != e; ++i) {
    if (i->Type == Atom::AT_Code || i->Type == Atom::AT_Data) {
      std::memcpy(&out.front() + i->RVA, i->Contents.data(),
                  i->Contents.size());

      // Apply relocations.
      for (Atom::LinkList_t::iterator li = i->Links.begin(),
                                      le = i->Links.end(); li != le; ++li) {
        if (li->Type == Link::LT_Relocation) {
          uint64_t rva = i->RVA + li->RelocAddr;
          switch (li->RelocType) {
          case COFF::IMAGE_REL_I386_DIR32:
            endian::write_le<uint32_t, unaligned>(
              &out.front() + rva, ph.ImageBase + li->Operands[0]->RVA);
            break;
          case COFF::IMAGE_REL_I386_DIR32NB:
            endian::write_le<uint32_t, unaligned>(
              &out.front() + rva, li->Operands[0]->RVA);
            break;
          case COFF::IMAGE_REL_I386_REL32:
            endian::write_le<int32_t, unaligned>(
              &out.front() + rva, li->Operands[0]->RVA - (rva + 4));
            break;
          default:
            llvm_unreachable("Unknown relocation type!");
          }
        }
      }
    }
  }

  // Write it all out.
  std::string msg;
  raw_fd_ostream outfs("a.exe", msg, raw_fd_ostream::F_Binary);
  outfs.write(&out.front(), out.size());
  outfs.flush();

  return 0;
}
