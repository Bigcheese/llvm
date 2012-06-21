//===-- llvm-nm.cpp - Symbol table dumping utility for llvm ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"

#include <map>
#include <string>

using namespace llvm;

namespace {
struct ToolInfo;
class Argument;
class CommandLineParser;

typedef bool ParseFunc(CommandLineParser&, StringRef, Argument&);

struct OptionInfo {
  unsigned int Kind : 16;
  unsigned int Priority : 8;
  unsigned int IsCaseSensitive : 1;
  const char * const *Prefixes;
  const char *Name;
  const char * const *MetaVars;
  const char *RenderString;
  unsigned int RenderValueIndicesCount;
  const unsigned int *RenderValueIndices;
  const OptionInfo *Alias;
  const ToolInfo *Tool;
  const ParseFunc *Parser;

  std::pair<bool, StringRef> matches(StringRef Arg) const {
    for (const char * const *Pre = Prefixes; *Pre != 0; ++Pre) {
      std::string Prefix(*Pre);
      Prefix += Name;
      if (Arg.startswith(Prefix))
        return std::make_pair(true, Arg.substr(Prefix.size()));
    }
    return std::make_pair(false, "");
  }
};

struct ToolInfo {
  const char * const *Prefixes;
  const char * const *Joiners;
  const char * PrefixTrim;
  const char * JoinerTrim;
  const OptionInfo *Options;

  bool hasPrefix(StringRef Arg) const {
    for (const char * const *i = Prefixes; *i != 0; ++i) {
      if (Arg.startswith(*i))
        return true;
    }
    return false;
  }

  std::pair<const OptionInfo *, StringRef> findOption(StringRef Arg) const {
    // TODO: Convert this to a std::lower_bound search.
    const OptionInfo *Winner = 0;
    StringRef Remaining;
    for (const OptionInfo *OI = Options; OI->RenderString != 0; ++OI) {
      std::pair<bool, StringRef> Match = OI->matches(Arg);
      if (Match.first) {
        if (!Winner) {
          Winner = OI;
          Remaining = Match.second;
          continue;
        }
        if (OI->Priority > Winner->Priority) {
          Winner = OI;
        }
      }
    }
    return std::make_pair(Winner, Remaining);
  }

  const OptionInfo *findNearest(StringRef Arg) const {
    const OptionInfo *Winner = 0;
    Arg = Arg.ltrim(PrefixTrim);
    Arg = Arg.substr(0, Arg.find_first_of(JoinerTrim));
    unsigned int Best = 0;
    for (const OptionInfo *OI = Options; OI->RenderString != 0; ++OI) {
      unsigned Dist = Arg.edit_distance(OI->Name, true, Best);
      if (!Best || Dist < Best) {
        Best = Dist;
        Winner = OI;
      }
    }
    return Winner;
  }
};

/// Argument represents a specific instance of an option parsed from the command
/// line.
class Argument {
  Argument() : Info(0) {}

public:
  typedef std::map<unsigned int, std::string> ValueMap;

  Argument(const OptionInfo * const OI) : Info(OI) {}

  void setValue(unsigned int Index, const std::string &Value) {
    Values[Index] = Value;
  }

  void setValues(ValueMap VM) {
    Values = VM;
  }

  void dump() const {
    if (!Info) {
      errs() << Values.find(0)->second;
      return;
    }
    StringRef RenderString(Info->RenderString);
    while (true) {
      StringRef::size_type Loc = RenderString.find_first_of('%');
      if (Loc == StringRef::npos) {
        errs() << RenderString;
        break;
      }
      errs() << RenderString.substr(0, Loc);
      if (Loc + 1 == RenderString.size())
        break;
      if (RenderString[Loc + 1] == '%')
        errs() << '%';
      else {
        unsigned int Index = 0;
        RenderString.substr(Loc + 1, 1).getAsInteger(10, Index);
        errs() << Values.find(Index)->second;
      }
      RenderString = RenderString.substr(Loc + 2);
    }
  }

private:
  const OptionInfo * const Info;
  ValueMap Values;
};

typedef std::vector<Argument*> ArgumentList;

class CommandLineParser {
public:
  typedef const char * const * CArg;

  CommandLineParser(int argc, CArg argv, const ToolInfo *T)
    : CurArg(argv), Argc(argc), Argv(argv), Tool(T) {}

  void parse() {
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
        if (OI->Parser(*this, Val, *A))
          ArgList.push_back(A);
        else
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

  const ArgumentList &getArgList() const { return ArgList; }

  CArg CurArg;

private:
  const int Argc;
  const CArg Argv;
  const ToolInfo *Tool;
  ArgumentList ArgList;
  BumpPtrAllocator ArgListAlloc;
};

const uint16_t option_input = 0;

enum LLDOptionKind {
  lld_entry,
  lld_entry_single
};

extern const ToolInfo LLDToolInfo;

const char * const LLDEntryMeta[] = {"entry", 0};
const char * const LLDMultiOnly[] = {"--", 0};
const char * const LLDMulti[] = {"-", "--", 0};
const char * const LLDSingle[] = {"-", 0};

struct ArgParseState {
  CommandLineParser::CArg CurArg;
  StringRef CurArgVal;
  Argument::ValueMap Values;
};

typedef std::pair<bool, ArgParseState> ArgParseResult;

template <typename PA, typename PB>
struct OrParser {
  OrParser(PA a, PB b) : A(a), B(b) {}

  ArgParseResult operator() (const ArgParseState APS) {
    ArgParseResult Res = A(APS);
    if (Res.first)
      return std::make_pair(true, Res.second);
    Res = B(APS);
    if (Res.first)
      return std::make_pair(true, Res.second);
    return std::make_pair(false, APS);
  }

  PA A;
  PB B;
};

template <typename PA, typename PB>
OrParser<PA, PB> parseOr(PA A, PB B) {
  return OrParser<PA, PB>(A, B);
}

template <typename Par>
struct JoinedParser {
  JoinedParser(StringRef Join, Par P) : Joiner(Join), Parser(P) {}

  ArgParseResult operator() (const ArgParseState APS) {
    if (APS.CurArgVal.startswith(Joiner)) {
      ArgParseState JoinerRemoved = APS;
      JoinerRemoved.CurArgVal = APS.CurArgVal.substr(Joiner.size());
      return Parser(JoinerRemoved);
    }
    return std::make_pair(false, APS);
  }

  StringRef Joiner;
  Par Parser;
};

template <typename Par>
JoinedParser<Par> parseJoined(StringRef Join, Par P) {
  return JoinedParser<Par>(Join, P);
}

template <typename Par>
struct SeperateParser {
  SeperateParser(Par P) : Parser(P) {}

  ArgParseResult operator() (const ArgParseState APS) {
    // A seperated argument must not be followed by anything except a space.
    if (!APS.CurArgVal.empty())
      return std::make_pair(false, APS);

    ArgParseState NextArg = APS;
    ++NextArg.CurArg;

    // Make sure the next arg isn't the end of the list.
    if (!*NextArg.CurArg)
      return std::make_pair(false, APS);

    NextArg.CurArgVal = *NextArg.CurArg;
    return Parser(NextArg);
  }

  Par Parser;
};

template <typename Par>
SeperateParser<Par> parseSeperate(Par P) {
  return SeperateParser<Par>(P);
}

struct StrParser {
  StrParser(unsigned int ValIndex) : ValueIndex(ValIndex) {}

  ArgParseResult operator() (const ArgParseState APS) {
    ArgParseState Ret = APS;
    Ret.Values[ValueIndex] = Ret.CurArgVal;
    Ret.CurArgVal.substr(Ret.CurArgVal.size());
    return std::make_pair(true, Ret);
  }

  unsigned int ValueIndex;
};

StrParser parseStr(unsigned int ValIndex) {
  return StrParser(ValIndex);
}

bool parseNullJoined( CommandLineParser &CLP
                    , StringRef ArgVal
                    , Argument &A) {
  ArgParseState APS;
  APS.CurArg = CLP.CurArg;
  APS.CurArgVal = ArgVal;
  ArgParseResult APR = parseJoined("", parseStr(0))(APS);
  if (!APR.first)
    return false;
  CLP.CurArg = APR.second.CurArg;
  A.setValues(APR.second.Values);
  return true;
}

bool parseJoinedOrSeperate( CommandLineParser &CLP
                          , StringRef ArgVal
                          , Argument &A) {
  ArgParseState APS;
  APS.CurArg = CLP.CurArg;
  APS.CurArgVal = ArgVal;
  ArgParseResult APR = parseOr(parseJoined("=", parseStr(0)),
                               parseSeperate(parseStr(0)))(APS);
  if (!APR.first)
    return false;
  CLP.CurArg = APR.second.CurArg;
  A.setValues(APR.second.Values);
  return true;
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

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.

  CommandLineParser lld(argc - 1, argv + 1, &LLDToolInfo);
  lld.parse();
  for (auto arg : lld.getArgList()) {
    arg->dump();
    errs() << " ";
  }
  errs() << "\n";
}
