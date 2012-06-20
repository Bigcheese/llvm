//===-- Option.cpp Tablegen driven option parsing library -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Option.h"

using namespace llvm;
using namespace option;

void CommandLineParser::parse() {
  // Parse all args, but allow CurArg to be changed while parsing.
  for (CArg e = Argv + Argc; CurArg != e; ++CurArg) {
    // See if it's a flag or an input.
    if (!Tool->hasPrefix(*CurArg)) {
      // An argument with no Info is an input.
      Argument *A = new (ArgListAlloc.Allocate<Argument>()) Argument(0);
      A->setValue(0, *CurArg);
      ArgList.push_back(A);
      continue;
    }
    // This argument has a valid prefix, so lets try to parse it.
    const OptionInfo *OI;
    StringRef Val;
    llvm::tie(OI, Val) = Tool->findOption(*CurArg);
    if (OI) {
      Argument *A = new (ArgListAlloc.Allocate<Argument>()) Argument(OI);
      ArgParseState APS;
      APS.CurArg = CurArg;
      APS.CurArgVal = Val;
      ArgParseResult APR = OI->Parser(APS);
      if (APR.first) {
        CurArg = APR.second.CurArg;
        A->setValues(APR.second.Values);
        ArgList.push_back(A);
      } else
        errs() << "Failed to parse " << *CurArg << "\n";
      continue;
    }
    // It looks like an option, but it's not one we know about. Try to get
    // typo-correction info.
    OI = Tool->findNearest(*CurArg);
    errs() << "Argument: " << *CurArg << " unknown";
    if (OI)
      errs() << " did you mean '" << OI->Name << "'?\n";
    else
      errs() << "\n";
  }
}
