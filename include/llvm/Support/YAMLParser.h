//===--- YAMLParser.h - Simple YAML parser --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This is a YAML 1.2 parser.
//
//  See http://www.yaml.org/spec/1.2/spec.html for the full standard.
//
//  This currently does not implement the following:
//    * Nested simple keys "{a: 1}: b".
//    * Multi-line literal folding.
//    * Tag resolution.
//    * UTF-16.
//    * BOMs anywhere other than the first code point in the file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_YAML_PARSER_H
#define LLVM_SUPPORT_YAML_PARSER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/SourceMgr.h"

#include <deque>
#include <utility>

namespace llvm {
class MemoryBuffer;
class SourceMgr;

namespace yaml {

enum UnicodeEncodingForm {
  UET_UTF32_LE,
  UET_UTF32_BE,
  UET_UTF16_LE,
  UET_UTF16_BE,
  UET_UTF8,     //< UTF-8 or ascii.
  UET_Unknown   //< Not a valid Unicode file.
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
EncodingInfo getUnicodeEncoding(StringRef input);

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
struct Token {
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

  /// A string of length 0 or more who's begin() points to the logical location
  /// of the token in the input.
  StringRef Range;

  StreamStartInfo StreamStart;
  VersionDirectiveInfo VersionDirective;
  ScalarInfo Scalar;

  Token() : Kind(TK_Error) {}
};

struct SimpleKey {
  const Token *Tok;
  unsigned Column;
  unsigned Line;
  unsigned FlowLevel;
  bool IsRequired;

  bool operator <(const SimpleKey &Other) {
    return Tok <  Other.Tok;
  }

  bool operator ==(const SimpleKey &Other) {
    return Tok == Other.Tok;
  }
};

/// @brief Scans YAML tokens from a MemoryBuffer.
class Scanner {
public:
  Scanner(const StringRef input, SourceMgr &sm);

  /// @brief Parse the next token and return it without poping it.
  Token &peekNext();

  /// @brief Parse the next token and pop it from the queue.
  Token getNext();

  void printError(SMLoc Loc, SourceMgr::DiagKind Kind, const Twine &Msg,
                  ArrayRef<SMRange> Ranges = ArrayRef<SMRange>()) {
    SM.PrintMessage(Loc, Kind, Msg, Ranges);
  }

  void setError(const Twine &Msg, StringRef::iterator Pos) {
    // Don't print out more errors after the first one we encounter. The rest
    // are just the result of the first, and have no meaning.
    if (!Failed)
      printError(SMLoc::getFromPointer(Pos), SourceMgr::DK_Error, Msg);
    Failed = true;
  }

  void setError(const Twine &Msg) {
    setError(Msg, Cur);
  }

  /// @brief Returns true if an error occured while parsing.
  bool failed() {
    return Failed;
  }

private:
  StringRef currentInput() {
    return StringRef(Cur, End - Cur);
  }

  bool isAtEnd(StringRef::iterator i = 0) {
    if (i)
      return i == End;
    return Cur == End;
  }

  /// @brief The Unicode scalar value of a UTF-8 code unit sequence and the
  ///        sequence's length in code units (uint8_t). A length of 0 represents
  ///        an error.
  typedef std::pair<uint32_t, unsigned> UTF8Decoded;

  /// @brief Decode a UTF-8 minimal well-formed code unit subsequence starting
  ///        at \a Pos.
  ///
  /// If the UTF-8 code units starting at Pos do not form a well-formed code
  /// unit subsequence, then the Unicode scalar value is 0, and the length is 0.
  UTF8Decoded decodeUTF8(StringRef::iterator Pos);

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

  /// @brief Skip minimal well-formed code unit subsequence's until Func
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

  StringRef scan_ns_uri_char();
  StringRef scan_ns_plain_one_line();
  bool consume(uint32_t expected);
  void skip(uint32_t Distance);
  bool isBlankOrBreak(StringRef::iterator Pos);
  void removeStaleSimpleKeys();
  void removeSimpleKeyOnFlowLevel(unsigned Level);
  bool unrollIndent(int Col);
  bool rollIndent( int Col
                 , Token::TokenKind Kind
                 , std::deque<Token>::iterator InsertPoint);
  void scanToNextToken();
  bool scanStreamStart();
  bool scanStreamEnd();
  bool scanDirective();
  bool scanDocumentIndicator(bool IsStart);
  bool scanFlowCollectionStart(bool IsSequence);
  bool scanFlowCollectionEnd(bool IsSequence);
  bool scanFlowEntry();
  bool scanBlockEntry();
  bool scanKey();
  bool scanValue();
  bool scanFlowScalar(bool IsDoubleQuoted);
  bool scanPlainScalar();
  bool scanAliasOrAnchor(bool IsAlias);
  bool scanBlockScalar(bool IsLiteral);
  bool scanTag();
  bool fetchMoreTokens();

  /// @brief The SourceMgr used for diagnostics and buffer management.
  SourceMgr &SM;

  /// @brief The origional input.
  MemoryBuffer &InputBuffer;

  /// @brief The current position of the scanner.
  StringRef::iterator Cur;

  /// @brief The end of the input (one past the last character).
  StringRef::iterator End;

  /// @brief Current YAML indentation level in spaces.
  int Indent;

  /// @brief Current column number in unicode code points.
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

  /// @brief True if an error has occured.
  bool Failed;

  /// @brief Queue of tokens. This is required to queue up tokens while looking
  ///        for the end of a simple key. And for cases where a single character
  ///        can produce multiple tokens (e.g. BlockEnd).
  std::deque<Token> TokenQueue;

  /// @brief Indentation levels.
  SmallVector<int, 4> Indents;

  /// @brief Potential simple keys.
  SmallVector<SimpleKey, 4> SimpleKeys;
};

}
}

#endif
