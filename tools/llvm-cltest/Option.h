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

#include "llvm/ADT/SmallString.h"
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

/// A ParseFunc function is used to parse a string into an Argument given the
/// Option it represents. ArgParseReslt.first is false if parsing failed. true
/// otherwise.
typedef ArgParseResult ParseFunc(const ArgParseState);

/// The OptionInfo class is a standard-layout class that represents a single
/// Option for a given Tool. For each tool, an array of them is aggregate
/// initalized from a TableGen file.
struct OptionInfo {
  /// Unique per Tool option kind. 
  unsigned int Kind : 16;
  /// Priority when multiple OptionInfos have the same prefix. The one with the
  /// highest priority is matched first.
  unsigned int Priority : 8;
  /// Should Name be matched case sensitive.
  unsigned int IsCaseSensitive : 1;
  /// A null terminated array of prefix strings to apply to name while matching.
  const char * const *Prefixes;
  /// The name of the option without any pre or postfixes. This is used for typo
  /// correction.
  const char *Name;
  /// A null terminated array of strings that represent metavariable names.
  /// These are used to display help text.
  const char * const *MetaVars;
  /// Model of how to render this option. %<number> are replaced with Argument
  /// values, or MetaVars values if no Argument values have been bound.
  const char *RenderString;
  /// The Option this Option aliases. May be null.
  const OptionInfo *Alias;
  /// The Tool this option belongs to.
  const ToolInfo *Tool;
  /// The ParseFunc to use to parse the values of this Option. Null if no
  /// parsing needed.
  ParseFunc *Parser;

  /// Check if \a Arg starts with any combination of Prefixes + Name.
  ///
  /// \returns A pair of did it match, and if true a StringRef of the exact part
  ///   of \a Arg that matched.
  std::pair<bool, StringRef> matches(StringRef Arg) const {
    for (const char * const *Pre = Prefixes; *Pre != 0; ++Pre) {
      std::string Prefix(*Pre);
      Prefix += Name;
      if (Arg.startswith(Prefix))
        return std::make_pair(true, Arg.substr(Prefix.size()));
    }
    return std::make_pair(false, "");
  }

  /// Print RenderString to \a OS filling in values using \a V.
  ///
  /// \a V must be a container that supports {OS << V[unsigned]}.
  template <class ValuesT>
  void dump(const ValuesT &V, raw_ostream &OS = errs()) const {
    StringRef RS(RenderString);
    while (true) {
      StringRef::size_type Loc = RS.find_first_of('%');
      if (Loc == StringRef::npos) {
        OS << RS;
        break;
      }
      OS << RS.substr(0, Loc);
      if (Loc + 1 == RS.size())
        break;
      if (RS[Loc + 1] == '%')
        OS << '%';
      else {
        unsigned int Index = 0;
        RS.substr(Loc + 1, 1).getAsInteger(10, Index);
        OS << V[Index];
      }
      RS = RS.substr(Loc + 2);
    }
  }
};

/// The ToolInfo class represents a single Tool. It is generated from a TableGen
/// file for each Tool.
struct ToolInfo {
  /// The union of each OptionInfo::Prefixes in Options. This is used to
  /// determine if a given string is a potential option or an input.
  const char * const *Prefixes;
  /// The union of all single characters from Prefixes. This is used to trim off
  /// characters prior to typo correction.
  const char * PrefixTrim;
  /// The union of each joiner character from each OptionInfo::ParseFunc in
  /// Options. This is used to strip off values prior to typo correction.
  const char * JoinerTrim;
  /// The list of all OptionInfos that belong to this Tool. The list must be
  /// sorted by Name then Priority.
  const OptionInfo *Options;

  /// Returns true if \a Arg starts with any string in Prefixes.
  bool hasPrefix(StringRef Arg) const {
    for (const char * const *i = Prefixes; *i != 0; ++i) {
      if (Arg.startswith(*i))
        return true;
    }
    return false;
  }

  /// Given a string, find which OptionInfo in Options is matches it.
  ///
  /// \returns <nullptr, ""> if no match found. Otherwise <match, rest-of-Arg>.
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

  /// Find the OptionInfo in Options that is the nearest match to \a Arg
  /// ignoring prefixes and joiners.
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

  /// Dump help text to \a OS using OptionInfo::MetaVars as the value set.
  void help(raw_ostream &OS) const {
    for (const OptionInfo *OI = Options; OI->RenderString != 0; ++OI) {
      OI->dump(OI->MetaVars);
      OS << "\n";
    }
  }
};

/// This template must be specialized for each ToolInfo's set of option enums to
/// return the ToolInfo associated with them.
template <class EnumType>
const ToolInfo *getToolInfoFromEnum(EnumType);

/// Argument represents a specific instance of an option parsed from the command
/// line.
class Argument {
  Argument() : Info(0), Claimed(false) {}

public:
  typedef SmallVector<SmallString<8>, 1> ValueMap;

  Argument(const OptionInfo * const OI) : Info(OI), Claimed(false) {}

  void setValue(unsigned int Index, StringRef Value) {
    Values.reserve(Index);
    Values[Index] = Value;
  }

  void setValues(ValueMap VM) {
    Values = VM;
  }

  const ValueMap &getValues() { return Values; }

  void dump(raw_ostream &OS = errs()) const {
    if (!Info) {
      assert(!Values.empty() && "Got input Argument with no Value!");
      OS << Values[0];
      return;
    }
    Info->dump(Values, OS);
  }

  void claim() {
    Claimed = true;
  }

  bool isClaimed() {
    return Claimed;
  }

  const OptionInfo * const Info;

private:
  /// The set of values.
  ValueMap Values;
  /// Has this Argument been used.
  bool Claimed;
};

/// A list of Arguments. Each Argument is owned by the Tool that parsed it.
typedef std::vector<Argument*> ArgumentList;

/// Get the last Argument of kind \a Opt from \a AL and claim it.
template <class T>
Argument *getLastArg(const ArgumentList &AL, T Opt) {
  const ToolInfo *TI = getToolInfoFromEnum(Opt);

  for (ArgumentList::const_reverse_iterator I = AL.rbegin(), E = AL.rend();
                                            I != E; ++I) {
    if ((*I)->Info && (*I)->Info->Kind == Opt && (*I)->Info->Tool == TI) {
      (*I)->claim();
      return *I;
    }
  }

  return 0;
}

/// See if \a AL has \a Opt and claim it if so.
template <class T>
bool hasArg(const ArgumentList &AL, T Opt) {
  return getLastArg(AL, Opt) != 0;
}

/// The CommandLineParser class is used by a Tool to parse argc and argv using
/// the provided ToolInfo. It owns the Arguments it creates.
class CommandLineParser {
public:
  typedef const char * const * CArg;

  CommandLineParser(int argc, CArg argv, const ToolInfo *T)
    : CurArg(argv), Argc(argc), Argv(argv), Tool(T) {}

  void parse();

  const ArgumentList &getArgList() const { return ArgList; }

  ArgumentList ArgList;
  BumpPtrAllocator ArgListAlloc;

private:
  CArg CurArg;

  const int Argc;
  const CArg Argv;
  const ToolInfo *Tool;
};

struct ArgParseState {
  CommandLineParser::CArg CurArg;
  StringRef CurArgVal;
  Argument::ValueMap Values;
};

/// Attempt to parse PA, if that fails, try PB.
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

/// Parse string joined by Join.
template <typename Par>
struct JoinedParser {
  JoinedParser(StringRef Join, Par P) : Joiner(Join), Parser(P) {}

  ArgParseResult operator() (const ArgParseState APS) {
    if (Joiner.empty() && APS.CurArgVal.empty())
      return std::make_pair(false, APS);
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

/// Parse a value seperated in argc.
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

/// Capture a string as a value.
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
