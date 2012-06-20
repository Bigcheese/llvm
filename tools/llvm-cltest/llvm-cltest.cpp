//===-- llvm-nm.cpp - Symbol table dumping utility for llvm ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"

#include <string>

using namespace llvm;

struct OptionInfo {
  const char *RenderString;
  const char **MetaVars;
  const char **Prefixes;
  const char *Name;
  bool IsCaseInsensitive;
  unsigned int Priority;
  const OptionInfo *Alias;
};

struct ToolInfo {
  const char **Prefixes;
  const OptionInfo *Options;
};

const char *LLDEntryMeta[] = {"entry", 0};
const char *LLDMulti[] = {"-", "--", 0};
const char *LLDSingle[] = {"-", 0};

const OptionInfo Ops[] = {
  {"--entry=$v1", LLDEntryMeta, LLDMulti, "entry", false, 0, 0},
  {"-e $v1", LLDEntryMeta, LLDSingle, "e", false, 1, 0},
  {0, 0, 0, false, 0, 0}
};

const ToolInfo LLDToolInfo = {LLDMulti, Ops};

bool fits(StringRef Flag, const OptionInfo *OI) {
  for (const char **Pre = OI->Prefixes; *Pre != 0; ++Pre) {
    if (Flag.startswith(std::string(*Pre) + OI->Name))
      return true;
  }
  return false;
}

void parseFlag(StringRef Flag) {
  const OptionInfo *Winner = 0;
  for (const OptionInfo *OI = LLDToolInfo.Options; OI->RenderString != 0;
                                                   ++OI) {
    if (fits(Flag, OI)) {
      if (!Winner) {
        Winner = OI;
        continue;
      }
      if (OI->Priority > Winner->Priority) {
        Winner = OI;
      }
    }
  }
  if (Winner)
    outs() << "Winner: " << Winner->Name << "\n";
  else {
    outs() << "No winner :(\n";
    // Find the next best name.
    Flag = Flag.ltrim("-");
    Flag = Flag.substr(0, Flag.find_first_of('='));
    unsigned int Best = 0;
    for (const OptionInfo *OI = LLDToolInfo.Options; OI->RenderString != 0;
                                                     ++OI) {
      unsigned Dist = Flag.edit_distance(OI->Name, true, Best);
      if (!Best || Dist < Best) {
        Best = Dist;
        Winner = OI;
      }
    }
    if (Winner) {
      outs() << "Did you mean: " << Winner->Name << "?\n";
    }
  }
}

void parseCmd(int argc, char **argv) {
  for (; argc; --argc, ++argv) {
    for (int i = 0; LLDToolInfo.Prefixes[i] != 0; ++i) {
      if (StringRef(*argv).startswith(LLDToolInfo.Prefixes[i])) {
        outs() << *argv << ": potential flag!\n";
        parseFlag(*argv);
        goto flag;
      }
    }
    outs() << *argv << ": not flag!\n";
flag:
    ;
  }
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  parseCmd(argc - 1, argv + 1);
}
