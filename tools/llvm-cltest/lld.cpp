//===-- lld.cpp Option parser for lld -------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lld.h"

using namespace llvm;
using namespace option;

// Everything in here gets tablegened.
namespace {
extern const ToolInfo LLDToolInfo;

const char * const LLDEntryMeta[] = {"entry", 0};
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
  {lld_entry_single, 0, true, LLDSingle, "e", LLDEntryMeta, "-e %0", 1, LLDRender1, 0, &LLDToolInfo, parseNullJoined},
  {0}
};

const char * const LLDJoiners[] = {"=", 0};

const ToolInfo LLDToolInfo = {LLDMulti, LLDJoiners, "-", "=", Ops};
} // end namespace

LLDTool::LLDTool(int Argc, const char * const *Argv)
  : CLP(Argc, Argv, &LLDToolInfo) {
  CLP.parse();
}
