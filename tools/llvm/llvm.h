//===-- llvm.h - The LLVM megatool ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the global flags for all tools, and the entry points for
// each individual tool.
//
// To add a tool to this program.
// * Add a new entry point here: int main_<toolname>(int argc, char **argv)
// * Add a new <toolname>.cpp file to the project that defines main_<toolname>
// * In llvm.cpp add a cl::subcommand option that links <toolname> to
//   main_<toolname>
// * Add options to <toolname>.cpp that have the cl::sub flag set to <toolname>
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_H
#define LLVM_TOOLS_LLVM_H

typedef int (*MainFunctionT)(int, char**);

int main_as(int, char**);

#endif // LLVM_TOOLS_LLVM_H
