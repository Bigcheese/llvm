//===-- link-options.h Option parser for the link driver ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "link-options.h"

using namespace llvm;
using namespace option;

// Everything in here gets tablegened.
namespace {
const char * const LinkLibraryMeta[] = {"library", 0};
const char * const LinkEntryMeta[] = {"function", 0};
const char * const LinkOutMeta[] = {"filename", 0};
const char * const LinkPathMeta[] = {"directory", 0};
const char * const LinkPrefix[] = {"-", "/", 0};

ArgParseResult parseColonJoined(const ArgParseState APS) {
  return parseJoined(":", parseStr(0))(APS);
}

const unsigned int LinkRender1[] = {0};

const OptionInfo Ops[] = {
  {link_default_lib, 0, false, LinkPrefix, "defaultlib", LinkLibraryMeta, "-defaultlib:%0", 1, LinkRender1, 0, &LinkToolInfo, parseColonJoined},
  {link_entry, 0, false, LinkPrefix, "entry", LinkEntryMeta, "-entry:%0", 1, LinkRender1, 0, &LinkToolInfo, parseColonJoined},
  {link_libpath, 0, false, LinkPrefix, "libpath", LinkPathMeta, "-libpath:%0", 1, LinkRender1, 0, &LinkToolInfo, parseColonJoined},
  {link_no_default_lib, 1, false, LinkPrefix, "nodefaultlib", LinkLibraryMeta, "-nodefaultlib:%0", 1, LinkRender1, 0, &LinkToolInfo, parseColonJoined},
  {link_no_default_lib_flag, 0, false, LinkPrefix, "nodefaultlib", 0, "-nodefaultlib", 0, 0, 0, &LinkToolInfo, 0},
  {link_out, 0, false, LinkPrefix, "out", LinkOutMeta, "-out:%0", 1, LinkRender1, 0, &LinkToolInfo, parseColonJoined},
  {0}
};

const char * const LinkJoiners[] = {":", 0};
} // end namespace

const ToolInfo llvm::option::LinkToolInfo = {LinkPrefix, LinkJoiners, "-", "=", Ops};

LinkTool::LinkTool(int Argc, const char * const *Argv)
  : CLP(Argc, Argv, &LinkToolInfo) {
  CLP.parse();
}
