//===-- lld.h Option parser for lld ---------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_LLD_H
#define LLVM_OPTION_LLD_H

#include "Option.h"

namespace llvm {
namespace option {

enum LLDOptionKind {
  lld_entry,
  lld_entry_single,
  lld_library_single
};

// This is needed to determine which tool a given argument is from by allowing
// the linker to assign unique ids.
extern const ToolInfo LLDToolInfo;

struct LLDTool {
  LLDTool(int Argc, const char * const *Argv);
  LLDTool(ArgumentList AL);

  ArgumentList getArgList() const { return CLP.getArgList(); };

  CommandLineParser CLP;
};

} // end namespace llvm
} // end namespace option

#endif
