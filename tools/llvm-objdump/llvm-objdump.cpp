//===-- llvm-objdump.cpp - Object file dumping utility for llvm -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This program is a utility that works like binutils "objdump", that is, it
// dumps out a plethora of information about an object file depending on the
// flags.
//
//===----------------------------------------------------------------------===//

#include "llvm-objdump.h"
#include "MCFunction.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/MemoryObject.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/system_error.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
using namespace llvm;
using namespace object;

static cl::list<std::string>
InputFilenames(cl::Positional, cl::desc("<input object files>"),cl::ZeroOrMore);

static cl::opt<bool>
Disassemble("disassemble",
  cl::desc("Display assembler mnemonics for the machine instructions"));
static cl::alias
Disassembled("d", cl::desc("Alias for --disassemble"),
             cl::aliasopt(Disassemble));

static cl::opt<bool>
Relocations("r", cl::desc("Display the relocation entries in the file"));

static cl::opt<bool>
Sections("h", cl::desc("Display the sections in the file"));

static cl::opt<bool>
MachO("macho", cl::desc("Use MachO specific object file parser"));
static cl::alias
MachOm("m", cl::desc("Alias for --macho"), cl::aliasopt(MachO));

cl::opt<std::string>
llvm::TripleName("triple", cl::desc("Target triple to disassemble for, "
                                    "see -version for available targets"));

cl::opt<std::string>
llvm::ArchName("arch", cl::desc("Target arch to disassemble for, "
                                "see -version for available targets"));

static StringRef ToolName;

template<typename T>
static int binary_log(T val) {
  if (val == 0)
    return -1;

  int ret = 0;
  while (val >>= 1) {
    ++ret;
  }
  return ret;
}

static bool error(error_code ec) {
  if (!ec) return false;

  outs() << ToolName << ": error reading file: " << ec.message() << ".\n";
  outs().flush();
  return true;
}

static const Target *GetTarget(const ObjectFile *Obj = NULL) {
  // Figure out the target triple.
  llvm::Triple TT("unknown-unknown-unknown");
  if (TripleName.empty()) {
    if (Obj)
      TT.setArch(Triple::ArchType(Obj->getArch()));
  } else
    TT.setTriple(Triple::normalize(TripleName));

  if (!ArchName.empty())
    TT.setArchName(ArchName);

  TripleName = TT.str();

  // Get the target specific parser.
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TripleName, Error);
  if (TheTarget)
    return TheTarget;

  errs() << ToolName << ": error: unable to get target for '" << TripleName
         << "', see --version and --triple.\n";
  return 0;
}

void llvm::DumpBytes(StringRef bytes) {
  static const char hex_rep[] = "0123456789abcdef";
  // FIXME: The real way to do this is to figure out the longest instruction
  //        and align to that size before printing. I'll fix this when I get
  //        around to outputting relocations.
  // 15 is the longest x86 instruction
  // 3 is for the hex rep of a byte + a space.
  // 1 is for the null terminator.
  enum { OutputSize = (15 * 3) + 1 };
  char output[OutputSize];

  assert(bytes.size() <= 15
    && "DumpBytes only supports instructions of up to 15 bytes");
  memset(output, ' ', sizeof(output));
  unsigned index = 0;
  for (StringRef::iterator i = bytes.begin(),
                           e = bytes.end(); i != e; ++i) {
    output[index] = hex_rep[(*i & 0xF0) >> 4];
    output[index + 1] = hex_rep[*i & 0xF];
    index += 3;
  }

  output[sizeof(output) - 1] = 0;
  outs() << output;
}

static void DisassembleObject(const ObjectFile *Obj) {
  const Target *TheTarget = GetTarget(Obj);
  if (!TheTarget) {
    // GetTarget prints out stuff.
    return;
  }

  error_code ec;
  for (section_iterator i = Obj->begin_sections(),
                        e = Obj->end_sections();
                        i != e; i.increment(ec)) {
    if (error(ec)) break;
    bool text;
    if (error(i->isText(text))) break;
    if (!text) continue;

    // Make a list of all the symbols in this section.
    std::vector<std::pair<uint64_t, StringRef> > Symbols;
    for (symbol_iterator si = Obj->begin_symbols(),
                         se = Obj->end_symbols();
                         si != se; si.increment(ec)) {
      bool contains;
      if (!error(i->containsSymbol(*si, contains)) && contains) {
        uint64_t Address;
        if (error(si->getOffset(Address))) break;
        StringRef Name;
        if (error(si->getName(Name))) break;
        Symbols.push_back(std::make_pair(Address, Name));
      }
    }

    // Sort the symbols by address, just in case they didn't come in that way.
    array_pod_sort(Symbols.begin(), Symbols.end());

    StringRef name;
    if (error(i->getName(name))) break;
    outs() << "Disassembly of section " << name << ':';

    // If the section has no symbols just insert a dummy one and disassemble
    // the whole section.
    if (Symbols.empty())
      Symbols.push_back(std::make_pair(0, name));

    // Set up disassembler.
    OwningPtr<const MCAsmInfo> AsmInfo(TheTarget->createMCAsmInfo(TripleName));

    if (!AsmInfo) {
      errs() << "error: no assembly info for target " << TripleName << "\n";
      return;
    }

    OwningPtr<const MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(TripleName, "", ""));

    if (!STI) {
      errs() << "error: no subtarget info for target " << TripleName << "\n";
      return;
    }

    OwningPtr<const MCDisassembler> DisAsm(
      TheTarget->createMCDisassembler(*STI));
    if (!DisAsm) {
      errs() << "error: no disassembler for target " << TripleName << "\n";
      return;
    }

    int AsmPrinterVariant = AsmInfo->getAssemblerDialect();
    OwningPtr<MCInstPrinter> IP(TheTarget->createMCInstPrinter(
                                AsmPrinterVariant, *AsmInfo, *STI));
    if (!IP) {
      errs() << "error: no instruction printer for target " << TripleName
             << '\n';
      return;
    }

    StringRef Bytes;
    if (error(i->getContents(Bytes))) break;
    StringRefMemoryObject memoryObject(Bytes);
    uint64_t Size;
    uint64_t Index;
    uint64_t SectSize;
    if (error(i->getSize(SectSize))) break;

    // Disassemble symbol by symbol.
    for (unsigned si = 0, se = Symbols.size(); si != se; ++si) {
      uint64_t Start = Symbols[si].first;
      uint64_t End = si == se-1 ? SectSize : Symbols[si + 1].first - 1;
      outs() << '\n' << Symbols[si].second << ":\n";

#ifndef NDEBUG
        raw_ostream &DebugOut = DebugFlag ? dbgs() : nulls();
#else
        raw_ostream &DebugOut = nulls();
#endif

      for (Index = Start; Index < End; Index += Size) {
        MCInst Inst;

        if (DisAsm->getInstruction(Inst, Size, memoryObject, Index,
                                   DebugOut, nulls())) {
          uint64_t addr;
          if (error(i->getAddress(addr))) break;
          outs() << format("%8x:\t", addr + Index);
          DumpBytes(StringRef(Bytes.data() + Index, Size));
          IP->printInst(&Inst, outs(), "");
          outs() << "\n";
        } else {
          errs() << ToolName << ": warning: invalid instruction encoding\n";
          if (Size == 0)
            Size = 1; // skip illegible bytes
        }
      }
    }
  }
}

static void PrintRelocations(const ObjectFile *o) {
  error_code ec;
  for (section_iterator si = o->begin_sections(), se = o->end_sections();
                                                  si != se; si.increment(ec)){
    if (error(ec)) return;
    if (si->begin_relocations() == si->end_relocations())
      continue;
    StringRef secname;
    if (error(si->getName(secname))) continue;
    outs() << "RELOCATION RECORDS FOR [" << secname << "]:\n";
    for (relocation_iterator ri = si->begin_relocations(),
                             re = si->end_relocations();
                             ri != re; ri.increment(ec)) {
      if (error(ec)) return;

      uint64_t address;
      SmallString<32> relocname;
      SmallString<32> valuestr;
      if (error(ri->getTypeName(relocname))) continue;
      if (error(ri->getAddress(address))) continue;
      if (error(ri->getValueString(valuestr))) continue;
      outs() << address << " " << relocname << " " << valuestr << "\n";
    }
    outs() << "\n";
  }
}

static void PrintSections(const ObjectFile *o) {
  int bw = o->getBytesInAddress() * 2;
  std::cout << "Sections:\n";
  std::cout << std::left
            << "Idx "
            << std::setw(14) << "Name"
            << std::setw(10) << "Size"
            << std::setw(2 + bw) << "VMA"
            << std::setw(2 + bw) << "LMA"
            << std::setw(10) << "File off"
            << "Algn\n";
  int idx = 0;
  error_code ec;
  for (section_iterator i = o->begin_sections(),
                        e = o->end_sections(); i != e; i.increment(ec), ++idx) {
    if (error(ec)) return;
    StringRef name;
    StringRef section_content;
    uint64_t size;
    uint64_t vma = 0;
    uint64_t lma = 0;
    uint64_t file_offset = 0;
    uint64_t align = 0;
    if (error(i->getName(name))) continue;
    if (error(i->getSize(size))) continue;
    if (error(i->getAlignment(align))) continue;
    align = binary_log(align);
    if (error(i->getContents(section_content))) continue;
    file_offset = section_content.begin() - o->getData().begin();
    std::cout << std::right << std::setfill(' ')
              << std::setw(3) << idx << " "
              << std::left << std::setw(14) << name.str()
              << std::right << std::hex << std::setfill('0')
              << std::setw(8) << size << "  "
              << std::setw(bw) << vma << "  "
              << std::setw(bw) << lma << "  "
              << std::setw(8) << file_offset << "  "
              << std::left << std::dec << std::setfill(' ') << "2**" << align
              << "\n";
  }
  std::cout << "\n";
}

static void DumpObject(const ObjectFile *o) {

  std::cout << "\n"
            << o->getFileName().str() << ":     file format "
            << o->getFileFormatName().str() << "\n\n";

  if (Sections)
    PrintSections(o);
  if (Disassemble)
    DisassembleObject(o);
  if (Relocations)
    PrintRelocations(o);
}

/// @brief Dump each object file in \a a;
static void DumpArchive(const Archive *a) {
  for (Archive::child_iterator i = a->begin_children(),
                               e = a->end_children(); i != e; ++i) {
    OwningPtr<Binary> child;
    if (error_code ec = i->getAsBinary(child)) {
      errs() << ToolName << ": '" << a->getFileName() << "': " << ec.message()
             << ".\n";
      continue;
    }
    if (ObjectFile *o = dyn_cast<ObjectFile>(child.get()))
      DumpObject(o);
    else
      errs() << ToolName << ": '" << a->getFileName() << "': "
              << "Unrecognized file type.\n";
  }
}

/// @brief Open file and figure out how to dump it.
static void DumpInput(StringRef file) {
  // If file isn't stdin, check that it exists.
  if (file != "-" && !sys::fs::exists(file)) {
    errs() << ToolName << ": '" << file << "': " << "No such file\n";
    return;
  }

  if (MachO && Disassemble) {
    DisassembleInputMachO(file);
    return;
  }

  // Attempt to open the binary.
  OwningPtr<Binary> binary;
  if (error_code ec = createBinary(file, binary)) {
    errs() << ToolName << ": '" << file << "': " << ec.message() << ".\n";
    return;
  }

  if (Archive *a = dyn_cast<Archive>(binary.get())) {
    DumpArchive(a);
  } else if (ObjectFile *o = dyn_cast<ObjectFile>(binary.get())) {
    DumpObject(o);
  } else {
    errs() << ToolName << ": '" << file << "': " << "Unrecognized file type.\n";
  }
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.

  // Initialize targets and assembly printers/parsers.
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllDisassemblers();

  cl::ParseCommandLineOptions(argc, argv, "llvm object file dumper\n");
  TripleName = Triple::normalize(TripleName);

  ToolName = argv[0];

  // Defaults to a.out if no filenames specified.
  if (InputFilenames.size() == 0)
    InputFilenames.push_back("a.out");

  if (!Disassemble && !Relocations && !Sections) {
    cl::PrintHelpMessage();
    return 2;
  }

  std::for_each(InputFilenames.begin(), InputFilenames.end(), DumpInput);

  return 0;
}
