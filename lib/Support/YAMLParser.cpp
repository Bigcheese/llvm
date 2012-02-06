//===--- YAMLParser.cpp - Simple YAML parser ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements a YAML parser.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/YAMLParser.h"

#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/SourceMgr.h"

using namespace llvm;
using namespace yaml;

namespace llvm {
namespace yaml {

enum UnicodeEncodingForm {
  UEF_UTF32_LE, //< UTF-32 Little Endian
  UEF_UTF32_BE, //< UTF-32 Big Endian
  UEF_UTF16_LE, //< UTF-16 Little Endian
  UEF_UTF16_BE, //< UTF-16 Big Endian
  UEF_UTF8,     //< UTF-8 or ascii.
  UEF_Unknown   //< Not a valid Unicode encoding.
};

/// EncodingInfo - Holds the encoding type and length of the byte order mark if
///                it exists. Length is in {0, 2, 3, 4}.
typedef std::pair<UnicodeEncodingForm, unsigned> EncodingInfo;

/// getBOM - Reads up to the first 4 bytes to determine the Unicode encoding
///          form of \a input.
///
/// @param input A string of length 0 or more.
/// @returns An EncodingInfo indicating the Unicode encoding form of the input
///          and how long the byte order mark is if one exists.
EncodingInfo getUnicodeEncoding(StringRef input) {
  if (input.size() == 0)
    return std::make_pair(UEF_Unknown, 0);

  switch (uint8_t(input[0])) {
  case 0x00:
    if (input.size() >= 4) {
      if (  input[1] == 0
         && uint8_t(input[2]) == 0xFE
         && uint8_t(input[3]) == 0xFF)
        return std::make_pair(UEF_UTF32_BE, 4);
      if (input[1] == 0 && input[2] == 0 && input[3] != 0)
        return std::make_pair(UEF_UTF32_BE, 0);
    }

    if (input.size() >= 2 && input[1] != 0)
      return std::make_pair(UEF_UTF16_BE, 0);
    return std::make_pair(UEF_Unknown, 0);
  case 0xFF:
    if (  input.size() >= 4
       && uint8_t(input[1]) == 0xFE
       && input[2] == 0
       && input[3] == 0)
      return std::make_pair(UEF_UTF32_LE, 4);

    if (input.size() >= 2 && uint8_t(input[1]) == 0xFE)
      return std::make_pair(UEF_UTF16_LE, 2);
    return std::make_pair(UEF_Unknown, 0);
  case 0xFE:
    if (input.size() >= 2 && uint8_t(input[1]) == 0xFF)
      return std::make_pair(UEF_UTF16_BE, 2);
    return std::make_pair(UEF_Unknown, 0);
  case 0xEF:
    if (  input.size() >= 3
       && uint8_t(input[1]) == 0xBB
       && uint8_t(input[2]) == 0xBF)
      return std::make_pair(UEF_UTF8, 3);
    return std::make_pair(UEF_Unknown, 0);
  }

  // It could still be utf-32 or utf-16.
  if (input.size() >= 4 && input[1] == 0 && input[2] == 0 && input[3] == 0)
    return std::make_pair(UEF_UTF32_LE, 0);

  if (input.size() >= 2 && input[1] == 0)
    return std::make_pair(UEF_UTF16_LE, 0);

  return std::make_pair(UEF_UTF8, 0);
}

struct StreamStartInfo {
  UnicodeEncodingForm Encoding;
};

struct VersionDirectiveInfo {
  StringRef Value;
};

struct ScalarInfo {
  StringRef Value;
};

/// Token - A single YAML token.
struct Token : ilist_node<Token> {
  enum TokenKind {
    TK_Error, // Uninitialized token.
    TK_StreamStart,
    TK_StreamEnd,
    TK_VersionDirective,
    TK_TagDirective,
    TK_DocumentStart,
    TK_DocumentEnd,
    TK_BlockEntry,
    TK_BlockEnd,
    TK_BlockSequenceStart,
    TK_BlockMappingStart,
    TK_FlowEntry,
    TK_FlowSequenceStart,
    TK_FlowSequenceEnd,
    TK_FlowMappingStart,
    TK_FlowMappingEnd,
    TK_Key,
    TK_Value,
    TK_Scalar,
    TK_Alias,
    TK_Anchor,
    TK_Tag
  } Kind;

  /// A string of length 0 or more whose begin() points to the logical location
  /// of the token in the input.
  StringRef Range;

  /// TODO: Stick these into a union. They currently aren't because StringRef
  ///       can't be in a union in C++03.
  StreamStartInfo StreamStart;
  VersionDirectiveInfo VersionDirective;
  ScalarInfo Scalar;

  Token() : Kind(TK_Error) {}
};

}

template<>
struct ilist_sentinel_traits<Token> {
  Token *createSentinel() const {
    return &Sentinel;
  }
  static void destroySentinel(Token*) {}

  Token *provideInitialHead() const { return createSentinel(); }
  Token *ensureHead(Token*) const { return createSentinel(); }
  static void noteHead(Token*, Token*) {}

private:
  mutable Token Sentinel;
};

template<>
struct ilist_node_traits<Token> {
  Token *createNode(const Token &V) {
    return new (Alloc.Allocate<Token>()) Token(V);
  }
  static void deleteNode(Token *V) {}

  void addNodeToList(Token *) {}
  void removeNodeFromList(Token *) {}
  void transferNodesFromList(ilist_node_traits &    /*SrcTraits*/,
                             ilist_iterator<Token> /*first*/,
                             ilist_iterator<Token> /*last*/) {}

  BumpPtrAllocator Alloc;
};

namespace yaml {

typedef ilist<Token> TokenQueueT;

/// @brief This struct is used to track simple keys.
///
/// Simple keys are handled by creating an entry in SimpleKeys for each Token
/// which could legally be the start of a simple key. When peekNext is called,
/// if the Token to be returned is referenced by a SimpleKey, we continue
/// tokenizing until that potential simple key has either been found to not be
/// a simple key (we moved on to the next line or went further than 1024 chars).
/// Or when we run into a Value, and then insert a Key token (and possibly
/// others) before the SimpleKey's Tok.
struct SimpleKey {
  TokenQueueT::iterator Tok;
  unsigned Column;
  unsigned Line;
  unsigned FlowLevel;
  bool IsRequired;

  bool operator ==(const SimpleKey &Other) {
    return Tok == Other.Tok;
  }
};

/// @brief Scans YAML tokens from a MemoryBuffer.
class Scanner {
public:
  Scanner(const StringRef input, SourceMgr &sm);

  /// @brief Parse the next token and return it without popping it.
  Token &peekNext();

  /// @brief Parse the next token and pop it from the queue.
  Token getNext();

  void printError(SMLoc Loc, SourceMgr::DiagKind Kind, const Twine &Msg,
                  ArrayRef<SMRange> Ranges = ArrayRef<SMRange>()) {
    SM.PrintMessage(Loc, Kind, Msg, Ranges);
  }

  void setError(const Twine &Msg, StringRef::iterator Pos) {
    if (Pos >= End)
      Pos = End - 1;

    // Don't print out more errors after the first one we encounter. The rest
    // are just the result of the first, and have no meaning.
    if (!Failed)
      printError(SMLoc::getFromPointer(Pos), SourceMgr::DK_Error, Msg);
    Failed = true;
  }

  void setError(const Twine &Msg) {
    setError(Msg, Cur);
  }

  /// @brief Returns true if an error occurred while parsing.
  bool failed() {
    return Failed;
  }

private:
  StringRef currentInput() {
    return StringRef(Cur, End - Cur);
  }

  /// @brief The Unicode scalar value of a UTF-8 minimal well-formed code unit
  ///        subsequence and the subsequence's length in code units (uint8_t).
  ///        A length of 0 represents an error.
  typedef std::pair<uint32_t, unsigned> UTF8Decoded;

  /// @brief Decode a UTF-8 minimal well-formed code unit subsequence starting
  ///        at \a Pos.
  ///
  /// If the UTF-8 code units starting at Pos do not form a well-formed code
  /// unit subsequence, then the Unicode scalar value is 0, and the length is 0.
  UTF8Decoded decodeUTF8(StringRef::iterator Pos);

  // The following functions are based on the gramar rules in the YAML spec. The
  // style of the function names it meant to closely match how they are written
  // in the spec. The number within the [] is the number of the grammar rule in
  // the spec.
  //
  // See 4.2 [Production Naming Conventions] for the meaning of the prefixes.

  /// @brief Skip a single nb-char[27] starting at Pos.
  ///
  /// A nb-char is 0x9 | [0x20-0x7E] | 0x85 | [0xA0-0xD7FF] | [0xE000-0xFEFE]
  ///                  | [0xFF00-0xFFFD] | [0x10000-0x10FFFF]
  ///
  /// @returns The code unit after the nb-char, or Pos if it's not an nb-char.
  StringRef::iterator skip_nb_char(StringRef::iterator Pos);

  /// @brief Skip a single b-break[28] starting at Pos.
  ///
  /// A b-break is 0xD 0xA | 0xD | 0xA
  ///
  /// @returns The code unit after the b-break, or Pos if it's not a b-break.
  StringRef::iterator skip_b_break(StringRef::iterator Pos);

  /// @brief Skip a single s-white[33] starting at Pos.
  ///
  /// A s-white is 0x20 | 0x9
  ///
  /// @returns The code unit after the s-white, or Pos if it's not a s-white.
  StringRef::iterator skip_s_white(StringRef::iterator Pos);

  /// @brief Skip a single ns-char[34] starting at Pos.
  ///
  /// A ns-char is nb-char - s-white
  ///
  /// @returns The code unit after the ns-char, or Pos if it's not a ns-char.
  StringRef::iterator skip_ns_char(StringRef::iterator Pos);

  /// @brief Skip minimal well-formed code unit subsequences until Func
  ///        returns its input.
  ///
  /// @returns The code unit after the last minimal well-formed code unit
  ///          subsequence that Func accepted.
  template<StringRef::iterator (Scanner::*Func)(StringRef::iterator)>
  StringRef::iterator skip_while(StringRef::iterator Pos) {
    while (true) {
      StringRef::iterator i = (this->*Func)(Pos);
      if (i == Pos)
        break;
      Pos = i;
    }
    return Pos;
  }

  /// @brief Scan ns-uri-char[39]s starting at Cur.
  ///
  /// This updates Cur and Column while scanning.
  ///
  /// @returns A StringRef starting at Cur which covers the longest contiguous
  ///          sequence of ns-uri-char.
  StringRef scan_ns_uri_char();

  /// @brief Scan ns-plain-one-line[133] starting at \a Cur.
  StringRef scan_ns_plain_one_line();

  /// @brief Consume a minimal well-formed code unit subsequence starting at
  ///        \a Cur. Return false if it is not the same Unicode scalar value as
  ///        \a expected. This updates \a Column.
  bool consume(uint32_t expected);

  /// @brief Skip \a Distance UTF-8 code units. Updates \a Cur and \a Column.
  void skip(uint32_t Distance);

  /// @brief Return true if the minimal well-formed code unit subsequence at
  ///        Pos is whitespace or a new line
  bool isBlankOrBreak(StringRef::iterator Pos);

  /// @brief If IsSimpleKeyAllowed, create and push_back a new SimpleKey.
  void saveSimpleKeyCandidate( TokenQueueT::iterator Tok
                             , unsigned Col
                             , bool IsRequired);

  /// @brief Remove simple keys that can no longer be valid simple keys.
  ///
  /// Invalid simple keys are not on the current line or are further than 1024
  /// columns back.
  void removeStaleSimpleKeyCandidates();

  /// @brief Remove all simple keys on FlowLevel \a Level.
  void removeSimpleKeyCandidatesOnFlowLevel(unsigned Level);

  /// @brief Unroll indentation in \a Indents back to \a Col. Creates BlockEnd
  ///        tokens if needed.
  bool unrollIndent(int Col);

  /// @brief Increase indent to \a Col. Creates \a Kind token at \a InsertPoint
  ///        if needed.
  bool rollIndent( int Col
                 , Token::TokenKind Kind
                 , TokenQueueT::iterator InsertPoint);

  /// @brief Skip whitespace and comments until the start of the next token.
  void scanToNextToken();

  /// @brief Must be the first token generated.
  bool scanStreamStart();

  /// @brief Generate tokens needed to close out the stream.
  bool scanStreamEnd();

  /// @brief Scan a %BLAH directive.
  bool scanDirective();

  /// @brief Scan a ... or ---.
  bool scanDocumentIndicator(bool IsStart);

  /// @brief Scan a [ or { and generate the proper flow collection start token.
  bool scanFlowCollectionStart(bool IsSequence);

  /// @brief Scan a ] or } and generate the proper flow collection end token.
  bool scanFlowCollectionEnd(bool IsSequence);

  /// @brief Scan the , that separates entries in a flow collection.
  bool scanFlowEntry();

  /// @brief Scan the - that starts block sequence entries.
  bool scanBlockEntry();

  /// @brief Scan an explicit ? indicating a key.
  bool scanKey();

  /// @brief Scan an explicit : indicating a value.
  bool scanValue();

  /// @brief Scan a quoted scalar.
  bool scanFlowScalar(bool IsDoubleQuoted);

  /// @brief Scan an unquoted scalar.
  bool scanPlainScalar();

  /// @brief Scan an Alias or Anchor starting with * or &.
  bool scanAliasOrAnchor(bool IsAlias);

  /// @brief Scan a block scalar starting with | or >.
  bool scanBlockScalar(bool IsLiteral);

  /// @brief Scan a tag of the form !stuff.
  bool scanTag();

  /// @brief Dispatch to the next scanning function based on \a *Cur.
  bool fetchMoreTokens();

  /// @brief The SourceMgr used for diagnostics and buffer management.
  SourceMgr &SM;

  /// @brief The original input.
  MemoryBuffer *InputBuffer;

  /// @brief The current position of the scanner.
  StringRef::iterator Cur;

  /// @brief The end of the input (one past the last character).
  StringRef::iterator End;

  /// @brief Current YAML indentation level in spaces.
  int Indent;

  /// @brief Current column number in Unicode code points.
  unsigned Column;

  /// @brief Current line number.
  unsigned Line;

  /// @brief How deep we are in flow style containers. 0 Means at block level.
  unsigned FlowLevel;

  /// @brief Are we at the start of the stream?
  bool IsStartOfStream;

  /// @brief Can the next token be the start of a simple key?
  bool IsSimpleKeyAllowed;

  /// @brief Is the next token required to start a simple key?
  bool IsSimpleKeyRequired;

  /// @brief True if an error has occurred.
  bool Failed;

  /// @brief Queue of tokens. This is required to queue up tokens while looking
  ///        for the end of a simple key. And for cases where a single character
  ///        can produce multiple tokens (e.g. BlockEnd).
  TokenQueueT TokenQueue;

  /// @brief Indentation levels.
  SmallVector<int, 4> Indents;

  /// @brief Potential simple keys.
  SmallVector<SimpleKey, 4> SimpleKeys;
};

} // end namespace yaml
} // end namespace llvm

bool yaml::dumpTokens(StringRef input, raw_ostream &OS) {
  SourceMgr SM;
  Scanner scanner(input, SM);
  while (true) {
    Token t = scanner.getNext();
    switch (t.Kind) {
    case Token::TK_StreamStart:
      OS << "Stream-Start(" << t.StreamStart.Encoding << "): ";
      break;
    case Token::TK_StreamEnd:
      OS << "Stream-End: ";
      break;
    case Token::TK_VersionDirective:
      OS << "Version-Directive(" << t.VersionDirective.Value << "): ";
      break;
    case Token::TK_TagDirective:
      OS << "Tag-Directive: ";
      break;
    case Token::TK_DocumentStart:
      OS << "Document-Start: ";
      break;
    case Token::TK_DocumentEnd:
      OS << "Document-End: ";
      break;
    case Token::TK_BlockEntry:
      OS << "Block-Entry: ";
      break;
    case Token::TK_BlockEnd:
      OS << "Block-End: ";
      break;
    case Token::TK_BlockSequenceStart:
      OS << "Block-Sequence-Start: ";
      break;
    case Token::TK_BlockMappingStart:
      OS << "Block-Mapping-Start: ";
      break;
    case Token::TK_FlowEntry:
      OS << "Flow-Entry: ";
      break;
    case Token::TK_FlowSequenceStart:
      OS << "Flow-Sequence-Start: ";
      break;
    case Token::TK_FlowSequenceEnd:
      OS << "Flow-Sequence-End: ";
      break;
    case Token::TK_FlowMappingStart:
      OS << "Flow-Mapping-Start: ";
      break;
    case Token::TK_FlowMappingEnd:
      OS << "Flow-Mapping-End: ";
      break;
    case Token::TK_Key:
      OS << "Key: ";
      break;
    case Token::TK_Value:
      OS << "Value: ";
      break;
    case Token::TK_Scalar:
      OS << "Scalar(" << t.Scalar.Value << "): ";
      break;
    case Token::TK_Alias:
      OS << "Alias(" << t.Scalar.Value << "): ";
      break;
    case Token::TK_Anchor:
      OS << "Anchor(" << t.Scalar.Value << "): ";
      break;
    case Token::TK_Tag:
      OS << "Tag: ";
      break;
    case Token::TK_Error:
      break;
    }
    OS << t.Range << "\n";
    if (t.Kind == Token::TK_StreamEnd)
      break;
    else if (t.Kind == Token::TK_Error)
      return false;
  }
  return true;
}

bool yaml::scanTokens(StringRef input) {
  llvm::SourceMgr SM;
  llvm::yaml::Scanner scanner(input, SM);
  for (;;) {
    llvm::yaml::Token t = scanner.getNext();
    if (t.Kind == Token::TK_StreamEnd)
      break;
    else if (t.Kind == Token::TK_Error)
      return false;
  }
  return true;
}

Scanner::Scanner(StringRef input, SourceMgr &sm)
  : SM(sm)
  , Indent(-1)
  , Column(0)
  , Line(0)
  , FlowLevel(0)
  , IsStartOfStream(true)
  , IsSimpleKeyAllowed(true)
  , IsSimpleKeyRequired(false)
  , Failed(false) {
  InputBuffer = MemoryBuffer::getMemBuffer(input, "YAML");
  SM.AddNewSourceBuffer(InputBuffer, SMLoc());
  Cur = InputBuffer->getBufferStart();
  End = InputBuffer->getBufferEnd();
}

Token &Scanner::peekNext() {
  // If the current token is a possible simple key, keep parsing until we
  // can confirm.
  bool NeedMore = false;
  while (true) {
    if (TokenQueue.empty() || NeedMore) {
      if (!fetchMoreTokens()) {
        TokenQueue.clear();
        TokenQueue.push_back(Token());
        return TokenQueue.front();
      }
    }
    assert(!TokenQueue.empty() &&
            "fetchMoreTokens lied about getting tokens!");

    removeStaleSimpleKeyCandidates();
    SimpleKey SK;
    SK.Tok = TokenQueue.front();
    if (std::find(SimpleKeys.begin(), SimpleKeys.end(), SK)
        == SimpleKeys.end())
      break;
    else
      NeedMore = true;
  }
  return TokenQueue.front();
}

Token Scanner::getNext() {
  Token ret = peekNext();
  // TokenQueue can be empty if there was an error getting the next token.
  if (!TokenQueue.empty())
    TokenQueue.pop_front();

  // There cannot be any referenced Token's if the TokenQueue is empty. So do a
  // quick deallocation of them all.
  if (TokenQueue.empty()) {
    TokenQueue.Alloc.Reset();
  }

  return ret;
}

Scanner::UTF8Decoded Scanner::decodeUTF8(StringRef::iterator Pos) {
  if ((uint8_t(*Pos) & 0x80) == 0)
    return std::make_pair(*Pos, 1);

  if (   (uint8_t(*Pos) & 0xE0) == 0xC0
      && Pos + 1 != End
      && uint8_t(*Pos) >= 0xC2
      && uint8_t(*Pos) <= 0xDF
      && uint8_t(*(Pos + 1)) >= 0x80
      && uint8_t(*(Pos + 1)) <= 0xBF) {
    uint32_t codepoint = uint8_t(*(Pos + 1)) & 0x3F;
    codepoint |= uint16_t(uint8_t(*Pos) & 0x1F) << 6;
    return std::make_pair(codepoint, 2);
  }

  if (   (uint8_t(*Pos) & 0xF0) == 0xE0
      && Pos + 2 < End) {
    if (   uint8_t(*Pos) == 0xE0
        && (  uint8_t(*(Pos + 1)) < 0xA0
          || uint8_t(*(Pos + 1)) > 0xBF));
    else if (  uint8_t(*Pos) >= 0xE1
            && uint8_t(*Pos) <= 0xEC
            && (  uint8_t(*(Pos + 1)) < 0x80
                || uint8_t(*(Pos + 1)) > 0xBF));
    else if (  uint8_t(*Pos) == 0xED
            && (  uint8_t(*(Pos + 1)) < 0x80
                || uint8_t(*(Pos + 1)) > 0x9F));
    else if (  uint8_t(*Pos) >= 0xEE
            && uint8_t(*Pos) <= 0xEF
            && (  uint8_t(*(Pos + 1)) < 0x80
                || uint8_t(*(Pos + 1)) > 0xBF));
    else {
      if (uint8_t(*(Pos + 2)) >= 0x80 && uint8_t(*(Pos + 2)) <= 0xBF) {
        uint32_t codepoint = uint8_t(*(Pos + 2)) & 0x3F;
        codepoint |= uint16_t(uint8_t(*(Pos + 1)) & 0x3F) << 6;
        codepoint |= uint16_t(uint8_t(*Pos) & 0x0F) << 12;
        return std::make_pair(codepoint, 3);
      }
    }
  }

  if (   (uint8_t(*Pos) & 0xF8) == 0xF0
      && Pos + 3 < End) {
    if (  uint8_t(*Pos) == 0xF0
        && (  uint8_t(*(Pos + 1)) < 0x90
          || uint8_t(*(Pos + 1)) > 0xBF));
    else if (  uint8_t(*Pos) >= 0xF1
            && uint8_t(*Pos) <= 0xF3
            && (  uint8_t(*(Pos + 1)) < 0x80
                || uint8_t(*(Pos + 1)) > 0xBF));
    else if (  uint8_t(*Pos) == 0xF4
            && (  uint8_t(*(Pos + 1)) < 0x80
                || uint8_t(*(Pos + 1)) > 0x8F));
    else {
      if (   uint8_t(*(Pos + 2)) >= 0x80 && uint8_t(*(Pos + 2)) <= 0xBF
          && uint8_t(*(Pos + 3)) >= 0x80 && uint8_t(*(Pos + 3)) <= 0xBF) {
        uint32_t codepoint = uint8_t(*(Pos + 3)) & 0x3F;
        codepoint |= uint32_t(uint8_t(*(Pos + 2)) & 0x3F) << 6;
        codepoint |= uint32_t(uint8_t(*(Pos + 1)) & 0x3F) << 12;
        codepoint |= uint32_t(uint8_t(*Pos) & 0x07) << 18;
        return std::make_pair(codepoint, 4);
      }
    }
  }

  // Not valid utf-8.
  setError("Invalid utf8 code unit", Pos);
  return std::make_pair(0, 0);
}

StringRef::iterator Scanner::skip_nb_char(StringRef::iterator Pos) {
  // Check 7 bit c-printable - b-char.
  if (   *Pos == 0x09
      || (*Pos >= 0x20 && *Pos <= 0x7E))
    return Pos + 1;

  // Check for valid utf-8.
  if (uint8_t(*Pos) & 0x80) {
    UTF8Decoded u8d = decodeUTF8(Pos);
    if (   u8d.second != 0
        && u8d.first != 0xFEFF
        && ( u8d.first == 0x85
          || ( u8d.first >= 0xA0
            && u8d.first <= 0xD7FF)
          || ( u8d.first >= 0xE000
            && u8d.first <= 0xFFFD)
          || ( u8d.first >= 0x10000
            && u8d.first <= 0x10FFFF)))
      return Pos + u8d.second;
  }
  return Pos;
}

StringRef::iterator Scanner::skip_b_break(StringRef::iterator Pos) {
  if (*Pos == 0x0D) {
    if (Pos + 1 != End && *(Pos + 1) == 0x0A)
      return Pos + 2;
    return Pos + 1;
  }

  if (*Pos == 0x0A)
    return Pos + 1;
  return Pos;
}


StringRef::iterator Scanner::skip_s_white(StringRef::iterator Pos) {
  if (Pos == End)
    return Pos;
  if (*Pos == ' ' || *Pos == '\t')
    return Pos + 1;
  return Pos;
}

StringRef::iterator Scanner::skip_ns_char(StringRef::iterator Pos) {
  if (Pos == End)
    return Pos;
  if (*Pos == ' ' || *Pos == '\t')
    return Pos;
  return skip_nb_char(Pos);
}

static bool is_ns_hex_digit(const char C) {
  if (  (C >= '0' && C <= '9')
     || (C >= 'a' && C <= 'z')
     || (C >= 'A' && C <= 'Z'))
    return true;
  return false;
}

static bool is_ns_word_char(const char C) {
  if (  C == '-'
     || (C >= 'a' && C <= 'z')
     || (C >= 'A' && C <= 'Z'))
    return true;
  return false;
}

StringRef Scanner::scan_ns_uri_char() {
  StringRef::iterator Start = Cur;
  while (true) {
    if (Cur == End)
      break;
    if ((   *Cur == '%'
          && Cur + 2 < End
          && is_ns_hex_digit(*(Cur + 1))
          && is_ns_hex_digit(*(Cur + 2)))
        || is_ns_word_char(*Cur)
        || StringRef(Cur, 1).find_first_of("#;/?:@&=+$,_.!~*'()[]")
          != StringRef::npos) {
      ++Cur;
      ++Column;
    } else
      break;
  }
  return StringRef(Start, Cur - Start);
}

StringRef Scanner::scan_ns_plain_one_line() {
  StringRef::iterator start = Cur;
  // The first character must already be verified.
  ++Cur;
  while (true) {
    if (Cur == End) {
      break;
    } else if (*Cur == ':') {
      // Check if the next character is a ns-char.
      if (Cur + 1 == End)
        break;
      StringRef::iterator i = skip_ns_char(Cur + 1);
      if (Cur + 1 != i) {
        Cur = i;
        Column += 2; // Consume both the ':' and ns-char.
      } else
        break;
    } else if (*Cur == '#') {
      // Check if the previous character was a ns-char.
      // The & 0x80 check is to check for the trailing byte of a utf-8
      if (*(Cur - 1) & 0x80 || skip_ns_char(Cur - 1) == Cur) {
        ++Cur;
        ++Column;
      } else
        break;
    } else {
      StringRef::iterator i = skip_nb_char(Cur);
      if (i == Cur)
        break;
      Cur = i;
      ++Column;
    }
  }
  return StringRef(start, Cur - start);
}

bool Scanner::consume(uint32_t expected) {
  if (expected >= 0x80)
    report_fatal_error("Not dealing with this yet");
  if (Cur == End)
    return false;
  if (uint8_t(*Cur) >= 0x80)
    report_fatal_error("Not dealing with this yet");
  if (uint8_t(*Cur) == expected) {
    ++Cur;
    ++Column;
    return true;
  }
  return false;
}

void Scanner::skip(uint32_t Distance) {
  Cur += Distance;
  Column += Distance;
}

bool Scanner::isBlankOrBreak(StringRef::iterator Pos) {
  if (Pos == End)
    return false;
  if (*Pos == ' ' || *Pos == '\t' || *Pos == '\r' || *Pos == '\n')
    return true;
  return false;
}

void Scanner::saveSimpleKeyCandidate( TokenQueueT::iterator Tok
                                    , unsigned Col
                                    , bool IsRequired) {
  if (IsSimpleKeyAllowed) {
    SimpleKey SK;
    SK.Tok = Tok;
    SK.Line = Line;
    SK.Column = Col;
    SK.IsRequired = IsRequired;
    SK.FlowLevel = FlowLevel;
    SimpleKeys.push_back(SK);
  }
}

void Scanner::removeStaleSimpleKeyCandidates() {
  for (SmallVectorImpl<SimpleKey>::iterator i = SimpleKeys.begin();
                                            i != SimpleKeys.end();) {
    if (i->Line != Line || i->Column + 1024 < Column) {
      if (i->IsRequired)
        setError( "Could not find expected : for simple key"
                , i->Tok->Range.begin());
      i = SimpleKeys.erase(i);
    } else
      ++i;
  }
}

void Scanner::removeSimpleKeyCandidatesOnFlowLevel(unsigned Level) {
  if (!SimpleKeys.empty() && (SimpleKeys.end() - 1)->FlowLevel == Level)
    SimpleKeys.pop_back();
}

bool Scanner::unrollIndent(int Col) {
  Token t;
  // Indentation is ignored in flow.
  if (FlowLevel != 0)
    return true;

  while (Indent > Col) {
    t.Kind = Token::TK_BlockEnd;
    t.Range = StringRef(Cur, 1);
    TokenQueue.push_back(t);
    Indent = Indents.pop_back_val();
  }

  return true;
}

bool Scanner::rollIndent( int Col
                        , Token::TokenKind Kind
                        , TokenQueueT::iterator InsertPoint) {
  if (FlowLevel)
    return true;
  if (Indent < Col) {
    Indents.push_back(Indent);
    Indent = Col;

    Token t;
    t.Kind = Kind;
    t.Range = StringRef(Cur, 0);
    TokenQueue.insert(InsertPoint, t);
  }
  return true;
}

void Scanner::scanToNextToken() {
  while (true) {
    while (*Cur == ' ' || *Cur == '\t') {
      skip(1);
    }

    // Skip comment.
    if (*Cur == '#') {
      while (true) {
        // This may skip more than one byte, thus Column is only incremented
        // for code points.
        StringRef::iterator i = skip_nb_char(Cur);
        if (i == Cur)
          break;
        Cur = i;
        ++Column;
      }
    }

    // Skip EOL.
    StringRef::iterator i = skip_b_break(Cur);
    if (i == Cur)
      break;
    Cur = i;
    ++Line;
    Column = 0;
    // New lines may start a simple key.
    if (!FlowLevel)
      IsSimpleKeyAllowed = true;
  }
}

bool Scanner::scanStreamStart() {
  IsStartOfStream = false;

  EncodingInfo EI = getUnicodeEncoding(currentInput());
  Cur += EI.second;

  Token t;
  t.Kind = Token::TK_StreamStart;
  t.StreamStart.Encoding = EI.first;
  TokenQueue.push_back(t);
  return true;
}

bool Scanner::scanStreamEnd() {
  // Force an ending new line if one isn't present.
  if (Column != 0) {
    Column = 0;
    ++Line;
  }

  unrollIndent(-1);
  SimpleKeys.clear();
  IsSimpleKeyAllowed = false;

  Token t;
  t.Kind = Token::TK_StreamEnd;
  t.Range = StringRef(Cur, 0);
  TokenQueue.push_back(t);
  return true;
}

bool Scanner::scanDirective() {
  // Reset the indentation level.
  unrollIndent(-1);
  SimpleKeys.clear();
  IsSimpleKeyAllowed = false;

  StringRef::iterator Start = Cur;
  consume('%');
  StringRef::iterator NameStart = Cur;
  Cur = skip_while<&Scanner::skip_ns_char>(Cur);
  StringRef Name(NameStart, Cur - NameStart);
  Cur = skip_while<&Scanner::skip_s_white>(Cur);

  if (Name == "YAML") {
    StringRef::iterator VersionStart = Cur;
    Cur = skip_while<&Scanner::skip_ns_char>(Cur);
    StringRef Version(VersionStart, Cur - VersionStart);
    Token t;
    t.Kind = Token::TK_VersionDirective;
    t.Range = StringRef(Start, Cur - Start);
    t.VersionDirective.Value = Version;
    TokenQueue.push_back(t);
    return true;
  }
  return false;
}

bool Scanner::scanDocumentIndicator(bool IsStart) {
  unrollIndent(-1);
  SimpleKeys.clear();
  IsSimpleKeyAllowed = false;

  Token t;
  t.Kind = IsStart ? Token::TK_DocumentStart : Token::TK_DocumentEnd;
  t.Range = StringRef(Cur, 3);
  skip(3);
  TokenQueue.push_back(t);
  return true;
}

bool Scanner::scanFlowCollectionStart(bool IsSequence) {
  Token t;
  t.Kind = IsSequence ? Token::TK_FlowSequenceStart
                      : Token::TK_FlowMappingStart;
  t.Range = StringRef(Cur, 1);
  skip(1);
  TokenQueue.push_back(t);

  // [ and { may begin a simple key.
  saveSimpleKeyCandidate(TokenQueue.back(), Column - 1, false);

  // And may also be followed by a simple key.
  IsSimpleKeyAllowed = true;
  ++FlowLevel;
  return true;
}

bool Scanner::scanFlowCollectionEnd(bool IsSequence) {
  removeSimpleKeyCandidatesOnFlowLevel(FlowLevel);
  IsSimpleKeyAllowed = false;
  Token t;
  t.Kind = IsSequence ? Token::TK_FlowSequenceEnd
                      : Token::TK_FlowMappingEnd;
  t.Range = StringRef(Cur, 1);
  skip(1);
  TokenQueue.push_back(t);
  if (FlowLevel)
    --FlowLevel;
  return true;
}

bool Scanner::scanFlowEntry() {
  removeSimpleKeyCandidatesOnFlowLevel(FlowLevel);
  IsSimpleKeyAllowed = true;
  Token t;
  t.Kind = Token::TK_FlowEntry;
  t.Range = StringRef(Cur, 1);
  skip(1);
  TokenQueue.push_back(t);
  return true;
}

bool Scanner::scanBlockEntry() {
  rollIndent(Column, Token::TK_BlockSequenceStart, TokenQueue.end());
  removeSimpleKeyCandidatesOnFlowLevel(FlowLevel);
  IsSimpleKeyAllowed = true;
  Token t;
  t.Kind = Token::TK_BlockEntry;
  t.Range = StringRef(Cur, 1);
  skip(1);
  TokenQueue.push_back(t);
  return true;
}

bool Scanner::scanKey() {
  if (!FlowLevel)
    rollIndent(Column, Token::TK_BlockMappingStart, TokenQueue.end());

  removeSimpleKeyCandidatesOnFlowLevel(FlowLevel);
  IsSimpleKeyAllowed = !FlowLevel;

  Token t;
  t.Kind = Token::TK_Key;
  t.Range = StringRef(Cur, 1);
  skip(1);
  TokenQueue.push_back(t);
  return true;
}

bool Scanner::scanValue() {
  // If the previous token could have been a simple key, insert the key token
  // into the token queue.
  if (!SimpleKeys.empty()) {
    SimpleKey SK = SimpleKeys.pop_back_val();
    Token t;
    t.Kind = Token::TK_Key;
    t.Range = SK.Tok->Range;
    TokenQueueT::iterator i, e;
    for (i = TokenQueue.begin(), e = TokenQueue.end(); i != e; ++i) {
      if (i == SK.Tok)
        break;
    }
    assert(i != e && "SimpleKey not in token queue!");
    i = TokenQueue.insert(i, t);

    // We may also need to add a Block-Mapping-Start token.
    rollIndent(SK.Column, Token::TK_BlockMappingStart, i);

    IsSimpleKeyAllowed = false;
  } else {
    if (!FlowLevel)
      rollIndent(Column, Token::TK_BlockMappingStart, TokenQueue.end());
    IsSimpleKeyAllowed = !FlowLevel;
  }

  Token t;
  t.Kind = Token::TK_Value;
  t.Range = StringRef(Cur, 1);
  skip(1);
  TokenQueue.push_back(t);
  return true;
}

// Forbidding inlining improves performance by roughly 20%.
// FIXME: Remove once llvm optimizes this to the faster version without hints.
LLVM_ATTRIBUTE_NOINLINE static bool
wasEscaped(StringRef::iterator First, StringRef::iterator Position);

// Returns whether a character at 'Position' was escaped with a leading '\'.
// 'First' specifies the position of the first character in the string.
static bool wasEscaped(StringRef::iterator First,
                       StringRef::iterator Position) {
  assert(Position - 1 >= First);
  StringRef::iterator I = Position - 1;
  // We calculate the number of consecutive '\'s before the current position
  // by iterating backwards through our string.
  while (I >= First && *I == '\\') --I;
  // (Position - 1 - I) now contains the number of '\'s before the current
  // position. If it is odd, the character at 'Position' was escaped.
  return (Position - 1 - I) % 2 == 1;
}

bool Scanner::scanFlowScalar(bool IsDoubleQuoted) {
  StringRef::iterator Start = Cur;
  unsigned ColStart = Column;
  if (IsDoubleQuoted) {
    do {
      ++Cur;
      while (Cur != End && *Cur != '"')
        ++Cur;
      // Repeat until the previous character was not a '\' or was an escaped
      // backslash.
    } while (*(Cur - 1) == '\\' && wasEscaped(Start + 1, Cur));
  } else {
    skip(1);
    while (true) {
      // Skip a ' followed by another '.
      if (Cur + 1 < End && *Cur == '\'' && *(Cur + 1) == '\'') {
        skip(2);
        continue;
      } else if (*Cur == '\'')
        break;
      StringRef::iterator i = skip_nb_char(Cur);
      if (i == Cur) {
        i = skip_b_break(Cur);
        if (i == Cur)
          break;
        Cur = i;
        Column = 0;
        ++Line;
      } else {
        if (i == End)
          break;
        Cur = i;
        ++Column;
      }
    }
  }
  StringRef Value(Start + 1, Cur - (Start + 1));
  skip(1); // Skip ending quote.
  Token t;
  t.Kind = Token::TK_Scalar;
  t.Range = StringRef(Start, Cur - Start);
  t.Scalar.Value = Value;
  TokenQueue.push_back(t);

  saveSimpleKeyCandidate(TokenQueue.back(), ColStart, false);

  IsSimpleKeyAllowed = false;

  return true;
}

bool Scanner::scanPlainScalar() {
  StringRef::iterator Start = Cur;
  unsigned ColStart = Column;
  unsigned LeadingBlanks = 0;
  assert(Indent >= -1 && "Indent must be >= -1 !");
  unsigned indent = static_cast<unsigned>(Indent + 1);
  while (true) {
    if (*Cur == '#')
      break;

    while (!isBlankOrBreak(Cur)) {
      if (  FlowLevel && *Cur == ':'
          && !(isBlankOrBreak(Cur + 1) || *(Cur + 1) == ',')) {
        setError("Found unexpected ':' while scanning a plain scalar", Cur);
        return false;
      }

      // Check for the end of the plain scalar.
      if (  (*Cur == ':' && isBlankOrBreak(Cur + 1))
          || (  FlowLevel
          && (StringRef(Cur, 1).find_first_of(",:?[]{}") != StringRef::npos)))
        break;

      StringRef::iterator i = skip_nb_char(Cur);
      if (i == Cur)
        break;
      Cur = i;
      ++Column;
    }

    // Are we at the end?
    if (!isBlankOrBreak(Cur))
      break;

    // Eat blanks.
    StringRef::iterator Tmp = Cur;
    while (isBlankOrBreak(Tmp)) {
      StringRef::iterator i = skip_s_white(Tmp);
      if (i != Tmp) {
        if (LeadingBlanks && (Column < indent) && *Tmp == '\t') {
          setError("Found invalid tab character in indentation", Tmp);
          return false;
        }
        Tmp = i;
        ++Column;
      } else {
        i = skip_b_break(Tmp);
        if (!LeadingBlanks)
          LeadingBlanks = 1;
        Tmp = i;
        Column = 0;
        ++Line;
      }
    }

    if (!FlowLevel && Column < indent)
      break;

    Cur = Tmp;
  }
  if (Start == Cur) {
    setError("Got empty plain scalar", Start);
    return false;
  }
  Token t;
  t.Kind = Token::TK_Scalar;
  t.Range = StringRef(Start, Cur - Start);
  t.Scalar.Value = t.Range;
  TokenQueue.push_back(t);

  // Plain scalars can be simple keys.
  saveSimpleKeyCandidate(TokenQueue.back(), ColStart, false);

  IsSimpleKeyAllowed = false;

  return true;
}

bool Scanner::scanAliasOrAnchor(bool IsAlias) {
  StringRef::iterator Start = Cur;
  unsigned ColStart = Column;
  skip(1);
  while(true) {
    if (   *Cur == '[' || *Cur == ']'
        || *Cur == '{' || *Cur == '}'
        || *Cur == ','
        || *Cur == ':')
      break;
    StringRef::iterator i = skip_ns_char(Cur);
    if (i == Cur)
      break;
    Cur = i;
    ++Column;
  }

  if (Start == Cur) {
    setError("Got empty alias or anchor", Start);
    return false;
  }

  Token t;
  t.Kind = IsAlias ? Token::TK_Alias : Token::TK_Anchor;
  t.Range = StringRef(Start, Cur - Start);
  t.Scalar.Value = t.Range.substr(1);
  TokenQueue.push_back(t);

  // Alias and anchors can be simple keys.
  saveSimpleKeyCandidate(TokenQueue.back(), ColStart, false);

  IsSimpleKeyAllowed = false;

  return true;
}

bool Scanner::scanBlockScalar(bool IsLiteral) {
  StringRef::iterator Start = Cur;
  skip(1); // Eat | or >
  while(true) {
    StringRef::iterator i = skip_nb_char(Cur);
    if (i == Cur) {
      if (Column == 0)
        break;
      i = skip_b_break(Cur);
      if (i != Cur) {
        // We got a line break.
        Column = 0;
        ++Line;
        Cur = i;
        continue;
      } else {
        // There was an error, which should already have been printed out.
        return false;
      }
    }
    Cur = i;
    ++Column;
  }

  if (Start == Cur) {
    setError("Got empty block scalar", Start);
    return false;
  }

  Token t;
  t.Kind = Token::TK_Scalar;
  t.Range = StringRef(Start, Cur - Start);
  t.Scalar.Value = t.Range;
  TokenQueue.push_back(t);
  return true;
}

bool Scanner::scanTag() {
  StringRef::iterator Start = Cur;
  unsigned ColStart = Column;
  skip(1); // Eat !.
  if (Cur == End || isBlankOrBreak(Cur)); // An empty tag.
  else if (*Cur == '<') {
    skip(1);
    StringRef VerbatiumTag = scan_ns_uri_char();
    if (!consume('>'))
      return false;
  } else {
    // FIXME: Actually parse the c-ns-shorthand-tag rule.
    Cur = skip_while<&Scanner::skip_ns_char>(Cur);
  }

  Token t;
  t.Kind = Token::TK_Tag;
  t.Range = StringRef(Start, Cur - Start);
  TokenQueue.push_back(t);

  // Tags can be simple keys.
  saveSimpleKeyCandidate(TokenQueue.back(), ColStart, false);

  IsSimpleKeyAllowed = false;

  return true;
}

bool Scanner::fetchMoreTokens() {
  if (IsStartOfStream)
    return scanStreamStart();

  scanToNextToken();

  if (Cur == End)
    return scanStreamEnd();

  removeStaleSimpleKeyCandidates();

  unrollIndent(Column);

  if (Column == 0 && *Cur == '%')
    return scanDirective();

  if (Column == 0 && Cur + 4 <= End
      && *Cur == '-'
      && *(Cur + 1) == '-'
      && *(Cur + 2) == '-'
      && (Cur + 3 == End || isBlankOrBreak(Cur + 3)))
    return scanDocumentIndicator(true);

  if (Column == 0 && Cur + 4 <= End
      && *Cur == '.'
      && *(Cur + 1) == '.'
      && *(Cur + 2) == '.'
      && (Cur + 3 == End || isBlankOrBreak(Cur + 3)))
    return scanDocumentIndicator(false);

  if (*Cur == '[')
    return scanFlowCollectionStart(true);

  if (*Cur == '{')
    return scanFlowCollectionStart(false);

  if (*Cur == ']')
    return scanFlowCollectionEnd(true);

  if (*Cur == '}')
    return scanFlowCollectionEnd(false);

  if (*Cur == ',')
    return scanFlowEntry();

  if (*Cur == '-' && isBlankOrBreak(Cur + 1))
    return scanBlockEntry();

  if (*Cur == '?' && (FlowLevel || isBlankOrBreak(Cur + 1)))
    return scanKey();

  if (*Cur == ':' && (FlowLevel || isBlankOrBreak(Cur + 1)))
    return scanValue();

  if (*Cur == '*')
    return scanAliasOrAnchor(true);

  if (*Cur == '&')
    return scanAliasOrAnchor(false);

  if (*Cur == '!')
    return scanTag();

  if (*Cur == '|' && !FlowLevel)
    return scanBlockScalar(true);

  if (*Cur == '>' && !FlowLevel)
    return scanBlockScalar(false);

  if (*Cur == '\'')
    return scanFlowScalar(false);

  if (*Cur == '"')
    return scanFlowScalar(true);

  // Get a plain scalar.
  StringRef FirstChar(Cur, 1);
  if (!(isBlankOrBreak(Cur)
        || FirstChar.find_first_of("-?:,[]{}#&*!|>'\"%@`") != StringRef::npos)
      || (*Cur == '-' && !isBlankOrBreak(Cur + 1))
      || (!FlowLevel && (*Cur == '?' || *Cur == ':')
          && isBlankOrBreak(Cur + 1))
      || (!FlowLevel && *Cur == ':'
                      && Cur + 2 < End
                      && *(Cur + 1) == ':'
                      && !isBlankOrBreak(Cur + 2)))
    return scanPlainScalar();

  setError("Unrecognized character while tokenizing.");
  return false;
}

Stream::Stream(StringRef input, SourceMgr &sm)
  : scanner(new Scanner(input, sm))
  , CurrentDoc(0) {}

Stream::~Stream() { delete CurrentDoc; }

bool Stream::failed() { return scanner->failed(); }

void Stream::handleYAMLDirective(const Token &t) {
  // TODO: Ensure version is 1.x.
}

document_iterator Stream::begin() {
  if (CurrentDoc)
    report_fatal_error("Can only iterate over the stream once");

  // Skip Stream-Start.
  scanner->getNext();

  CurrentDoc = new Document(*this);
  return document_iterator(CurrentDoc);
}

document_iterator Stream::end() {
  return document_iterator();
}

void Stream::skip() {
  for (document_iterator i = begin(), e = end(); i != e; ++i)
    i->skip();
}

Node::Node(unsigned int Type, Document *D, StringRef A)
  : Doc(D)
  , TypeID(Type)
  , Anchor(A)
{}

Node::~Node() {}

Token &Node::peekNext() {
  return Doc->peekNext();
}

Token Node::getNext() {
  return Doc->getNext();
}

Node *Node::parseBlockNode() {
  return Doc->parseBlockNode();
}

BumpPtrAllocator &Node::getAllocator() {
  return Doc->NodeAllocator;
}

void Node::setError(const Twine &Msg, Token &Tok) {
  Doc->setError(Msg, Tok);
}

bool Node::failed() const {
  return Doc->failed();
}

Node *KeyValueNode::getKey() {
  if (Key)
    return Key;
  // Handle implicit null keys.
  {
    Token &t = peekNext();
    if (   t.Kind == Token::TK_BlockEnd
        || t.Kind == Token::TK_Value
        || t.Kind == Token::TK_Error) {
      return Key = new (getAllocator().Allocate<NullNode>()) NullNode(Doc);
    }
    if (t.Kind == Token::TK_Key)
      getNext(); // skip TK_Key.
  }

  // Handle explicit null keys.
  Token &t = peekNext();
  if (t.Kind == Token::TK_BlockEnd || t.Kind == Token::TK_Value) {
    return Key = new (getAllocator().Allocate<NullNode>()) NullNode(Doc);
  }

  // We've got a normal key.
  return Key = parseBlockNode();
}

Node *KeyValueNode::getValue() {
  if (Value)
    return Value;
  getKey()->skip();
  if (failed())
    return Value = new (getAllocator().Allocate<NullNode>()) NullNode(Doc);

  // Handle implicit null values.
  {
    Token &t = peekNext();
    if (   t.Kind == Token::TK_BlockEnd
        || t.Kind == Token::TK_FlowMappingEnd
        || t.Kind == Token::TK_Key
        || t.Kind == Token::TK_FlowEntry
        || t.Kind == Token::TK_Error) {
      return Value = new (getAllocator().Allocate<NullNode>()) NullNode(Doc);
    }

    if (t.Kind != Token::TK_Value) {
      setError("Unexpected token in Key Value.", t);
      return Value = new (getAllocator().Allocate<NullNode>()) NullNode(Doc);
    }
    getNext(); // skip TK_Value.
  }

  // Handle explicit null values.
  Token &t = peekNext();
  if (t.Kind == Token::TK_BlockEnd || t.Kind == Token::TK_Key) {
    return Value = new (getAllocator().Allocate<NullNode>()) NullNode(Doc);
  }

  // We got a normal value.
  return Value = parseBlockNode();
}

void MappingNode::increment() {
  if (failed()) {
    IsAtEnd = true;
    CurrentEntry = 0;
    return;
  }
  if (CurrentEntry) {
    CurrentEntry->skip();
    if (Type == MT_Inline) {
      IsAtEnd = true;
      CurrentEntry = 0;
      return;
    }
  }
  Token t = peekNext();
  if (t.Kind == Token::TK_Key || t.Kind == Token::TK_Scalar) {
    // KeyValueNode eats the TK_Key. That way it can detect null keys.
    CurrentEntry = new (getAllocator().Allocate<KeyValueNode>())
      KeyValueNode(Doc);
  } else if (Type == MT_Block) {
    switch (t.Kind) {
    case Token::TK_BlockEnd:
      getNext();
      IsAtEnd = true;
      CurrentEntry = 0;
      break;
    default:
      setError("Unexpected token. Expected Key or Block End", t);
    case Token::TK_Error:
      IsAtEnd = true;
      CurrentEntry = 0;
    }
  } else {
    switch (t.Kind) {
    case Token::TK_FlowEntry:
      // Eat the flow entry and recurse.
      getNext();
      return increment();
    case Token::TK_FlowMappingEnd:
      getNext();
    case Token::TK_Error:
      // Set this to end iterator.
      IsAtEnd = true;
      CurrentEntry = 0;
      break;
    default:
      setError( "Unexpected token. Expected Key, Flow Entry, or Flow "
                "Mapping End."
              , t);
      IsAtEnd = true;
      CurrentEntry = 0;
    }
  }
}

void SequenceNode::increment() {
  if (failed()) {
    IsAtEnd = true;
    CurrentEntry = 0;
    return;
  }
  if (CurrentEntry)
    CurrentEntry->skip();
  Token t = peekNext();
  if (SeqType == ST_Block) {
    switch (t.Kind) {
    case Token::TK_BlockEntry:
      getNext();
      CurrentEntry = parseBlockNode();
      if (CurrentEntry == 0) { // An error occurred.
        IsAtEnd = true;
        CurrentEntry = 0;
      }
      break;
    case Token::TK_BlockEnd:
      getNext();
      IsAtEnd = true;
      CurrentEntry = 0;
      break;
    default:
      setError( "Unexpected token. Expected Block Entry or Block End."
              , t);
    case Token::TK_Error:
      IsAtEnd = true;
      CurrentEntry = 0;
    }
  } else if (SeqType == ST_Indentless) {
    switch (t.Kind) {
    case Token::TK_BlockEntry:
      getNext();
      CurrentEntry = parseBlockNode();
      if (CurrentEntry == 0) { // An error occurred.
        IsAtEnd = true;
        CurrentEntry = 0;
      }
      break;
    default:
    case Token::TK_Error:
      IsAtEnd = true;
      CurrentEntry = 0;
    }
  } else if (SeqType == ST_Flow) {
    switch (t.Kind) {
    case Token::TK_FlowEntry:
      // Eat the flow entry and recurse.
      getNext();
      WasPreviousTokenFlowEntry = true;
      return increment();
    case Token::TK_FlowSequenceEnd:
      getNext();
    case Token::TK_Error:
      // Set this to end iterator.
      IsAtEnd = true;
      CurrentEntry = 0;
      break;
    case Token::TK_StreamEnd:
    case Token::TK_DocumentEnd:
    case Token::TK_DocumentStart:
      setError("Could not find closing ]!", t);
      // Set this to end iterator.
      IsAtEnd = true;
      CurrentEntry = 0;
      break;
    default:
      if (!WasPreviousTokenFlowEntry) {
        setError("Expected , between entries!", t);
        IsAtEnd = true;
        CurrentEntry = 0;
        break;
      }
      // Otherwise it must be a flow entry.
      CurrentEntry = parseBlockNode();
      if (!CurrentEntry) {
        IsAtEnd = true;
      }
      WasPreviousTokenFlowEntry = false;
      break;
    }
  }
}

Document::Document(Stream &s) : stream(s), Root(0) {
  if (parseDirectives())
    expectToken(Token::TK_DocumentStart);
  Token &t = peekNext();
  if (t.Kind == Token::TK_DocumentStart)
    getNext();
}

bool Document::skip()  {
  if (stream.scanner->failed())
    return false;
  if (!Root)
    getRoot();
  Root->skip();
  Token &t = peekNext();
  if (t.Kind == Token::TK_StreamEnd)
    return false;
  if (t.Kind == Token::TK_DocumentEnd) {
    getNext();
    return skip();
  }
  return true;
}

Token &Document::peekNext() {
  return stream.scanner->peekNext();
}

Token Document::getNext() {
  return stream.scanner->getNext();
}

void Document::setError(const Twine &Msg, Token &Tok) {
  stream.scanner->setError(Msg, Tok.Range.begin());
}

bool Document::failed() const {
  return stream.scanner->failed();
}

Node *Document::parseBlockNode() {
  Token t = peekNext();
  // Handle properties.
  Token AnchorInfo;
parse_property:
  switch (t.Kind) {
  case Token::TK_Alias:
    getNext();
    return new (NodeAllocator.Allocate<AliasNode>())
      AliasNode(this, t.Scalar.Value);
  case Token::TK_Anchor:
    if (AnchorInfo.Kind == Token::TK_Anchor) {
      setError("Already encountered an anchor for this node!", t);
      return 0;
    }
    AnchorInfo = getNext(); // Consume TK_Anchor.
    t = peekNext();
    goto parse_property;
  case Token::TK_Tag:
    getNext(); // Skip TK_Tag.
    t = peekNext();
    goto parse_property;
  default:
    break;
  }

  switch (t.Kind) {
  case Token::TK_BlockEntry:
    // We got an unindented BlockEntry sequence. This is not terminated with
    // a BlockEnd.
    // Don't eat the TK_BlockEntry, SequenceNode needs it.
    return new (NodeAllocator.Allocate<SequenceNode>())
      SequenceNode( this
                  , AnchorInfo.Scalar.Value
                  , SequenceNode::ST_Indentless);
  case Token::TK_BlockSequenceStart:
    getNext();
    return new (NodeAllocator.Allocate<SequenceNode>())
      SequenceNode(this, AnchorInfo.Scalar.Value, SequenceNode::ST_Block);
  case Token::TK_BlockMappingStart:
    getNext();
    return new (NodeAllocator.Allocate<MappingNode>())
      MappingNode(this, AnchorInfo.Scalar.Value, MappingNode::MT_Block);
  case Token::TK_FlowSequenceStart:
    getNext();
    return new (NodeAllocator.Allocate<SequenceNode>())
      SequenceNode(this, AnchorInfo.Scalar.Value, SequenceNode::ST_Flow);
  case Token::TK_FlowMappingStart:
    getNext();
    return new (NodeAllocator.Allocate<MappingNode>())
      MappingNode(this, AnchorInfo.Scalar.Value, MappingNode::MT_Flow);
  case Token::TK_Scalar:
    getNext();
    return new (NodeAllocator.Allocate<ScalarNode>())
      ScalarNode(this, AnchorInfo.Scalar.Value, t.Scalar.Value);
  case Token::TK_Key:
    // Don't eat the TK_Key, KeyValueNode expects it.
    return new (NodeAllocator.Allocate<MappingNode>())
      MappingNode(this, AnchorInfo.Scalar.Value, MappingNode::MT_Inline);
  case Token::TK_DocumentStart:
  case Token::TK_DocumentEnd:
  case Token::TK_StreamEnd:
  default:
    // TODO: Properly handle tags. "[!!str ]" should resolve to !!str "", not
    //       !!null null.
    return new (NodeAllocator.Allocate<NullNode>()) NullNode(this);
  case Token::TK_Error:
    return 0;
  }
  llvm_unreachable("Control flow shouldn't reach here.");
  return 0;
}

bool Document::parseDirectives() {
  bool isDirective = false;
  while (true) {
    Token t = peekNext();
    if (t.Kind == Token::TK_TagDirective) {
      handleTagDirective(getNext());
      isDirective = true;
    } else if (t.Kind == Token::TK_VersionDirective) {
      stream.handleYAMLDirective(getNext());
      isDirective = true;
    } else
      break;
  }
  return isDirective;
}

bool Document::expectToken(int TK) {
  Token t = getNext();
  if (t.Kind != TK) {
    setError("Unexpected token", t);
    return false;
  }
  return true;
}

Document *document_iterator::NullDoc = 0;
