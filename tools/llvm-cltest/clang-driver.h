//===-- clang-driver.h Option parser for the clang driver -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_CLANG_DRIVER_H
#define LLVM_OPTION_CLANG_DRIVER_H

#include "Option.h"

namespace llvm {
namespace option {

enum ClangDriverOptionKind {
  clang_driver_library_single
};

extern const ToolInfo ClangDriverToolInfo;

struct ClangDriverTool {
  ClangDriverTool(int Argc, const char * const *Argv);

  ArgumentList getArgList() const { return CLP.getArgList(); };

  CommandLineParser CLP;
};

} // end namespace llvm
} // end namespace option

#endif
