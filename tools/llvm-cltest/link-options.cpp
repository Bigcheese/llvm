//===-- link-options.h Option parser for the link driver ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "link-options.h"
#include "clang-driver.h"

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

const OptionInfo             Ops[] = {
  {link_default_lib,         0, false, LinkPrefix, "defaultlib",   LinkLibraryMeta, "-defaultlib:%0",   0, &LinkToolInfo, parseColonJoined},
  {link_entry,               0, false, LinkPrefix, "entry",        LinkEntryMeta,   "-entry:%0",        0, &LinkToolInfo, parseColonJoined},
  {link_libpath,             0, false, LinkPrefix, "libpath",      LinkPathMeta,    "-libpath:%0",      0, &LinkToolInfo, parseColonJoined},
  {link_no_default_lib,      1, false, LinkPrefix, "nodefaultlib", LinkLibraryMeta, "-nodefaultlib:%0", 0, &LinkToolInfo, parseColonJoined},
  {link_no_default_lib_flag, 0, false, LinkPrefix, "nodefaultlib", 0,               "-nodefaultlib",    0, &LinkToolInfo, 0},
  {link_out,                 0, false, LinkPrefix, "out",          LinkOutMeta,     "-out:%0",          0, &LinkToolInfo, parseColonJoined},
  {0}
};

} // end namespace

const ToolInfo llvm::option::LinkToolInfo = {LinkPrefix, "-", "=", Ops};

LinkTool::LinkTool(int Argc, const char * const *Argv)
  : CLP(Argc, Argv, &LinkToolInfo) {
  CLP.parse();
}

LinkTool::LinkTool(const ArgumentList AL) 
  : CLP(0, 0, 0) {
  for (auto &A : AL) {
    if (!A->Info) {
      // Render as input.
      Argument *Arg = new (CLP.ArgListAlloc.Allocate<Argument>()) Argument(0);
      Arg->setValues(A->getValues());
      CLP.ArgList.push_back(Arg);
      continue;
    }
    if (A->Info->Tool != &ClangDriverToolInfo)
      continue;
    if (A->Info->Kind == clang_driver_library_path_single) {
      Argument *Arg = new (CLP.ArgListAlloc.Allocate<Argument>()) Argument(&Ops[link_libpath]);
      Arg->setValues(A->getValues());
      CLP.ArgList.push_back(Arg);
    } else if (A->Info->Kind == clang_driver_library_single) {
      // Render as input.
      Argument *Arg = new (CLP.ArgListAlloc.Allocate<Argument>()) Argument(0);
      std::string Val = A->getValues()[0].str();
      if (!StringRef(Val).endswith(".lib"))
        Val += ".lib";
      Arg->setValue(0, llvm_move(Val));
      CLP.ArgList.push_back(Arg);
    } else if (A->Info->Kind == clang_driver_output_single) {
      Argument *Arg = new (CLP.ArgListAlloc.Allocate<Argument>()) Argument(&Ops[link_out]);
      Arg->setValues(A->getValues());
      CLP.ArgList.push_back(Arg);
    }
  }
}
