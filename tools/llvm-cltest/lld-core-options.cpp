//===-- lld-core-options.h Option parser for lld-core ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lld-core-options.h"
#include "link-options.h"

using namespace llvm;
using namespace option;

// Everything in here gets tablegened.
namespace {
const char * const FileNameMeta[] = {"filename", 0};
const char * const PassNameMeta[] = {"pass", 0};
const char * const Single[] = {"-", 0};

ArgParseResult parseJoined(const ArgParseState APS) {
  return parseJoined("=", parseStr(0))(APS);
}

const unsigned int Render1[] = {0};

const OptionInfo Ops[] = {
  {lld_core_commons_search_archives, 0, true, Single, "commons-search-archives", 0,            "-commons-search-archives", 0, 0,       0, &LLDCoreToolInfo, 0},
  {lld_core_dead_strip,              0, true, Single, "dead-strip",              0,            "-dead-strip",              0, 0,       0, &LLDCoreToolInfo, 0},
  {lld_core_keep_globals,            0, true, Single, "keep-globals",            0,            "-keep-globals",            0, 0,       0, &LLDCoreToolInfo, 0},
  {lld_core_output,                  0, true, Single, "output",                  FileNameMeta, "-output=%0",               1, Render1, 0, &LLDCoreToolInfo, parseJoined},
  {lld_core_pass,                    0, true, Single, "pass",                    PassNameMeta, "-pass=%0",                 1, Render1, 0, &LLDCoreToolInfo, parseJoined},
  {lld_core_undefines_are_errors,    0, true, Single, "undefines-are-errors",    0,            "-undefines-are-errors",    0, 0,       0, &LLDCoreToolInfo, 0},
  {0}
};

const char * const Joiners[] = {"=", 0};
} // end namespace

const ToolInfo llvm::option::LLDCoreToolInfo = {Single, Joiners, "-", "=", Ops};

LLDCoreTool::LLDCoreTool(int Argc, const char * const *Argv)
  : CLP(Argc, Argv, &LLDCoreToolInfo) {
  CLP.parse();
}

LLDCoreTool::LLDCoreTool(const ArgumentList AL)
  : CLP(0, 0, 0) {
  for (auto &A : AL) {
    if (!A->Info) {
      Argument *Arg = new (CLP.ArgListAlloc.Allocate<Argument>()) Argument(0);
      Arg->setValues(A->getValues());
      CLP.ArgList.push_back(Arg);
      continue;
    }
    if (A->Info->Tool == &LinkToolInfo) {
      if (A->Info->Kind == link_out) {
        Argument *Arg = new (CLP.ArgListAlloc.Allocate<Argument>()) Argument(&Ops[lld_core_output]);
        Arg->setValues(A->getValues());
        CLP.ArgList.push_back(Arg);
      }
    }
  }
}
