//===-- clang-driver.cpp Option parser for the clang driver ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang-driver.h"

using namespace llvm;
using namespace option;

// Everything in here gets tablegened.
namespace {
const char * const ClangDriverLibraryMeta[] = {"library", 0};
const char * const ClangDriverSingle[] = {"-", 0};

ArgParseResult parseNullJoined(const ArgParseState APS) {
  return parseJoined("", parseStr(0))(APS);
}

const unsigned int ClangDriverRender1[] = {0};

const OptionInfo Ops[] = {
  {clang_driver_library_single, 1, true, ClangDriverSingle, "l", ClangDriverLibraryMeta, "-l%0", 1, ClangDriverRender1, 0, &ClangDriverToolInfo, parseNullJoined},
  {0}
};

const char * const ClangDriverJoiners[] = {"=", 0};
} // end namespace

const ToolInfo llvm::option::ClangDriverToolInfo = {ClangDriverSingle, ClangDriverJoiners, "-", "=", Ops};

ClangDriverTool::ClangDriverTool(int Argc, const char * const *Argv)
  : CLP(Argc, Argv, &ClangDriverToolInfo) {
  CLP.parse();
}
