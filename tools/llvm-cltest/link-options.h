//===-- link-options.h Option parser for the link driver ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_LINK_OPTIONS_H
#define LLVM_OPTION_LINK_OPTIONS_H

#include "Option.h"

namespace llvm {
namespace option {

enum LinkOptionKind {
  link_default_lib,
  link_entry,
  link_libpath,
  link_no_default_lib,
  link_no_default_lib_flag,
  link_out
};

extern const ToolInfo LinkToolInfo;

template <>
inline const ToolInfo *getToolInfoFromEnum<LinkOptionKind>(LinkOptionKind) {
  return &LinkToolInfo;
}

struct LinkTool {
  LinkTool(int Argc, const char * const *Argv);
  LinkTool(const ArgumentList);

  ArgumentList getArgList() const { return CLP.getArgList(); };

  CommandLineParser CLP;
};

} // end namespace llvm
} // end namespace option

#endif
