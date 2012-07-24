//===-- llvm-cltest.cpp - Command line test -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"

#include "clang-driver.h"
#include "link-options.h"
#include "lld.h"
#include "lld-core-options.h"

#include <map>
#include <string>

using namespace llvm;
using namespace option;

struct Blah {
  const char *Data;
  unsigned int Length;
};

void dumpArgList(const ArgumentList &AL) {
  for (auto A : AL) {
    A->dump();
    errs() << " ";
  }
  errs() << "\n";
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.

  if (argc < 2) {
    LLDCoreToolInfo.help(errs());
    return 0;
  }

  ClangDriverTool clang(argc - 1, argv + 1);
  dumpArgList(clang.getArgList());

  errs() << (hasArg(clang.getArgList(), clang_driver_output_single) ? "true" : "false") << "\n";

  for (auto &Arg : clang.getArgList()) {
    if (!Arg->isClaimed()) {
      errs() << "Warning: Unused argument: ";
      Arg->dump(errs());
      errs() << "\n";
    }
  }

  LinkTool link(clang.getArgList());
  dumpArgList(link.getArgList());
  LLDCoreTool lld_core(link.getArgList());
  dumpArgList(lld_core.getArgList());
}
