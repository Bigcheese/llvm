//===-- Option.h Tablegen driven option parsing library -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_OPTION_H
#define LLVM_OPTION_OPTION_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <vector>

namespace llvm {
namespace option {
struct ToolInfo;
class Argument;
class CommandLineParser;
struct ArgParseState;
typedef std::pair<bool, ArgParseState> ArgParseResult;

typedef ArgParseResult ParseFunc(const ArgParseState);

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

  const ValueMap &getValues() { return Values; }

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

  const OptionInfo * const Info;

private:
  ValueMap Values;
};

typedef std::vector<Argument*> ArgumentList;

class CommandLineParser {
public:
  typedef const char * const * CArg;

  CommandLineParser(int argc, CArg argv, const ToolInfo *T)
    : CurArg(argv), Argc(argc), Argv(argv), Tool(T) {}

  void parse();

  const ArgumentList &getArgList() const { return ArgList; }

  CArg CurArg;
  ArgumentList ArgList;
  BumpPtrAllocator ArgListAlloc;

private:
  const int Argc;
  const CArg Argv;
  const ToolInfo *Tool;
};

struct ArgParseState {
  CommandLineParser::CArg CurArg;
  StringRef CurArgVal;
  Argument::ValueMap Values;
};

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

inline StrParser parseStr(unsigned int ValIndex) {
  return StrParser(ValIndex);
}

} // end namespace option
} // end namespace llvm

#endif
