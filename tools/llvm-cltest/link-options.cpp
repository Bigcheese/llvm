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
const char * const LinkFooMeta[] = {"foo", 0};
const char * const LinkBarMeta[] = {"bar", 0};
const char * const LinkNopeMeta[] = {"nope", 0};
const char * const LinkBizMeta[] = {"biz", 0};
const char * const LinkPrefix[] = {"-", "/", 0};

ArgParseResult parseColonJoined(const ArgParseState APS) {
  return parseJoined(":", parseStr(0))(APS);
}

const OptionInfo             Ops[] = {
  {link_foo,  0, false, LinkPrefix, "foo",  LinkFooMeta,  "-foo:%0",  0, &LinkToolInfo, parseColonJoined},
  {link_bar,  0, false, LinkPrefix, "bar",  LinkBarMeta,  "-bar:%0",  0, &LinkToolInfo, parseColonJoined},
  {link_biz,  0, false, LinkPrefix, "biz",  LinkBizMeta,  "-biz:%0",  0, &LinkToolInfo, parseColonJoined},
  {link_baz,  1, false, LinkPrefix, "baz",  LinkFooMeta,  "-baz:%0",  0, &LinkToolInfo, parseColonJoined},
  {link_ayep, 0, false, LinkPrefix, "ayep", 0,            "-ayep",    0, &LinkToolInfo, 0},
  {link_nope, 0, false, LinkPrefix, "nope", LinkNopeMeta, "-nope:%0", 0, &LinkToolInfo, parseColonJoined},
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
      Argument *Arg = new (CLP.ArgListAlloc.Allocate<Argument>()) Argument(&Ops[link_biz]);
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
      Argument *Arg = new (CLP.ArgListAlloc.Allocate<Argument>()) Argument(&Ops[link_nope]);
      Arg->setValues(A->getValues());
      CLP.ArgList.push_back(Arg);
    }
  }
}
