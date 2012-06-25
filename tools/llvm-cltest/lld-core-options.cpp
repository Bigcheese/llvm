//===-- lld-core-options.h Option parser for lld-core ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lld-core-options.h"

using namespace llvm;
using namespace option;

// Everything in here gets tablegened.
namespace {
const char * const LLDCoreEntryMeta[]   = {"entry", 0};
const char * const LLDCoreLibraryMeta[] = {"library", 0};
const char * const LLDCoreMultiOnly[]   = {"--", 0};
const char * const LLDCoreMulti[]       = {"-", "--", 0};
const char * const LLDCoreSingle[]      = {"-", 0};

ArgParseResult parseNullJoined(const ArgParseState APS) {
  return parseJoined("", parseStr(0))(APS);
}

ArgParseResult parseJoinedOrSeperate(const ArgParseState APS) {
  return parseOr(parseJoined("=", parseStr(0)),
                 parseSeperate(parseStr(0)))(APS);
}

const unsigned int LLDCoreRender1[] = {0};

const OptionInfo Ops[] = {
  {0}
};

const char * const LLDCoreJoiners[] = {"=", 0};
} // end namespace

const ToolInfo llvm::option::LLDCoreToolInfo = {LLDCoreMulti, LLDCoreJoiners, "-", "=", Ops};

LLDCoreTool::LLDCoreTool(int Argc, const char * const *Argv)
  : CLP(Argc, Argv, &LLDCoreToolInfo) {
  CLP.parse();
}
