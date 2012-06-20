//===-- lld.cpp Option parser for lld -------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lld.h"
#include "clang-driver.h"

using namespace llvm;
using namespace option;

// Everything in here gets tablegened.
namespace {
const char * const LLDEntryMeta[] = {"entry", 0};
const char * const LLDLibraryMeta[] = {"library", 0};
const char * const LLDMultiOnly[] = {"--", 0};
const char * const LLDMulti[] = {"-", "--", 0};
const char * const LLDSingle[] = {"-", 0};

ArgParseResult parseNullJoined(const ArgParseState APS) {
  return parseJoined("", parseStr(0))(APS);
}

ArgParseResult parseJoinedOrSeperate(const ArgParseState APS) {
  return parseOr(parseJoined("=", parseStr(0)),
                 parseSeperate(parseStr(0)))(APS);
}

const unsigned int LLDRender1[] = {0};

const OptionInfo Ops[] = {
  {lld_entry, 0, true, LLDMultiOnly, "entry", LLDEntryMeta, "--entry=%0", 1, LLDRender1, 0, &LLDToolInfo, parseJoinedOrSeperate},
  {lld_entry_single, 1, true, LLDSingle, "e", LLDEntryMeta, "-e%0", 1, LLDRender1, &Ops[lld_entry], &LLDToolInfo, parseNullJoined},
  {lld_library_single, 1, true, LLDSingle, "l", LLDLibraryMeta, "-l%0", 1, LLDRender1, 0, &LLDToolInfo, parseNullJoined},
  {0}
};

const char * const LLDJoiners[] = {"=", 0};
} // end namespace

const ToolInfo llvm::option::LLDToolInfo = {LLDMulti, LLDJoiners, "-", "=", Ops};

LLDTool::LLDTool(int Argc, const char * const *Argv)
  : CLP(Argc, Argv, &LLDToolInfo) {
  CLP.parse();
}

LLDTool::LLDTool(ArgumentList AL)
  : CLP(0, 0, 0) { // TODO: Don't initalize CLP here...
  for (ArgumentList::const_iterator i = AL.begin(), e = AL.end(); i != e; ++i) {
    if ((*i)->Info->Kind == clang_driver_library_single &&
        (*i)->Info->Tool == &ClangDriverToolInfo) {
      Argument *A = new (CLP.ArgListAlloc.Allocate<Argument>()) Argument(&Ops[lld_library_single]);
      A->setValues((*i)->getValues());
      CLP.ArgList.push_back(A);
    }
  }
}
