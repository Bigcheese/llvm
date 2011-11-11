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
#include "llvm/Object/Context.h"
#include "llvm/Object/Module.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"
#include <algorithm>
#include <map>
#include <string>
#include <sstream>
#include <vector>
using namespace llvm;
using namespace object;

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
  // Atom *start = output->getOrCreateAtom(C.getName("_mainCRTStartup"));
  Atom *start = output->getOrCreateAtom(C.getName("_main"));
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
              Atom *a = m->getOrCreateAtom(i->Name);
              a->External = true;
              a->Defined = true;
              a->Import = true;
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
          i->Instance = ModMap[o]->getOrCreateAtom(i->Name);
        } else {
          Module *m = new Module(C);
          Modules.push_back(m);
          ModMap[o] = m;
          if (error(m->mergeObject(o))) continue;
          i->Instance = m->getOrCreateAtom(i->Name);
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

  // Stuff it all into the output module.
  for (std::size_t i = 1; i < Modules.size(); ++i) {
    output->mergeModule(Modules[i]);
  }

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
        for (std::vector<Link>::const_iterator li = ai->Links.begin(),
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

  return 0;
}
