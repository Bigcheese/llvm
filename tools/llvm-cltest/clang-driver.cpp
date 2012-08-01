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
const char * const ClangDriverPathMeta[] = {"libpath", 0};
const char * const ClangDriverFilePathMeta[] = {"filepath", 0};
const char * const ClangDriverSingle[] = {"-", 0};

ArgParseResult parseNullJoinedOrSeparate(const ArgParseState APS) {
  return parseOr(parseJoined("", parseStr(0)),
                 parseSeperate(parseStr(0)))(APS);
}

const unsigned int ClangDriverRender1[] = {0};

const OptionInfo Ops[] = {
  {clang_driver_library_path_single, 0, true, ClangDriverSingle, "L", ClangDriverPathMeta,     "-L%0",  0, &ClangDriverToolInfo, parseNullJoinedOrSeparate},
  {clang_driver_library_single,      1, true, ClangDriverSingle, "l", ClangDriverLibraryMeta,  "-l%0",  0, &ClangDriverToolInfo, parseNullJoinedOrSeparate},
  {clang_driver_output_single,       0, true, ClangDriverSingle, "o", ClangDriverFilePathMeta, "-o %0", 0, &ClangDriverToolInfo, parseNullJoinedOrSeparate},
  {0}
};

} // end namespace

const ToolInfo llvm::option::ClangDriverToolInfo = {ClangDriverSingle, "-", "=", Ops};

ClangDriverTool::ClangDriverTool(int Argc, const char * const *Argv)
  : CLP(Argc, Argv, &ClangDriverToolInfo) {
  CLP.parse();
}
