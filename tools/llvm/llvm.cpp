//===-- llvm.cpp - The LLVM megatool --------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the LLVM megatool.
// TODO: Explain what that means ;/
//
//===----------------------------------------------------------------------===//

#include "llvm.h"
#include "llvm/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"
using namespace llvm;

static int main_test(int argc, char **argv) {
  outs() << "test! " << argc << "\n";
}

/*static cl::subcommand command(
  "test", main_test, "This command tests the subcommand system!",
  0
  );*/

enum ActionType {
  AC_AsLex,
  AC_Assemble,
  AC_Disassemble,
  AC_EDisassemble
};

static cl::opt<ActionType>
Action(cl::desc("Action to perform:"),
       cl::init(AC_Assemble),
       cl::values(clEnumValN(AC_AsLex, "as-lex",
                             "Lex tokens from a .s file"),
                  clEnumValN(AC_Assemble, "assemble",
                             "Assemble a .s file (default)"),
                  clEnumValN(AC_Disassemble, "disassemble",
                             "Disassemble strings of hex bytes"),
                  clEnumValN(AC_EDisassemble, "edis",
                             "Enhanced disassembly of strings of hex bytes"),
                  clEnumValEnd));

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.

  cl::ParseCommandLineOptions(argc, argv);

  // return command.invoke(argc - 1, argv + 1);
}
