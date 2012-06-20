//===- OptionParserEmitter.cpp - Generate option parser information -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This tablegen backend emits option parsing information.
//
//===----------------------------------------------------------------------===//

#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringMatcher.h"
#include "llvm/TableGen/TableGenBackend.h"

#include <map>
#include <set>
#include <vector>

namespace llvm {

void EmitOptionParser(RecordKeeper &RK, raw_ostream &OS) {
  std::set<std::vector<std::string> > Prefixes;

  std::vector<Record*> Options = RK.getAllDerivedDefinitions("Option");
  for (std::vector<Record*>::iterator i = Options.begin(), e = Options.end();
       i != e; ++i) {
    OS << (*i)->getName() << "\n";
  }
}

}
