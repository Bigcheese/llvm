//===- YAMLBench - Benchmark the YAMLParser implementation ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This program executes the YAMLParser on differntly sized YAML texts and
// outputs the run time.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/SmallVector.h"

#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/system_error.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/SourceMgr.h"

#include <algorithm>
#include <deque>
#include <utility>

namespace llvm {
namespace yaml {

enum BOMType {
  BT_UTF32_LE,
  BT_UTF32_BE,
  BT_UTF16_LE,
  BT_UTF16_BE,
  BT_UTF8,
  BT_Unknown
};

typedef std::pair<BOMType, unsigned> BOMInfo;

// Returns the BOM and how many bytes to skip.
BOMInfo getBOM(StringRef input) {
  if (input.size() == 0)
    return std::make_pair(BT_Unknown, 0);

  switch (input[0]) {
  case 0x00:
    if (input.size() >= 4) {
      if (input[1] == 0 && input[2] == 0xFE && input[3] == 0xFF)
        return std::make_pair(BT_UTF32_BE, 4);
      if (input[1] == 0 && input[2] == 0 && input[3] != 0)
        return std::make_pair(BT_UTF32_BE, 0);
    }

    if (input.size() >= 2 && input[1] != 0)
      return std::make_pair(BT_UTF16_BE, 0);
    return std::make_pair(BT_Unknown, 0);
  case 0xFF:
    if (input.size() >= 4 && input[1] == 0xFE && input[2] == 0 && input[3] == 0)
      return std::make_pair(BT_UTF32_LE, 4);

    if (input.size() >= 2 && input[1] == 0xFE)
      return std::make_pair(BT_UTF16_LE, 2);
    return std::make_pair(BT_Unknown, 0);
  case 0xFE:
    if (input.size() >= 2 && input[1] == 0xFF)
      return std::make_pair(BT_UTF16_BE, 2);
    return std::make_pair(BT_Unknown, 0);
  case 0xEF:
    if (input.size() >= 3 && input[1] == 0xBB && input[2] == 0xBF)
      return std::make_pair(BT_UTF8, 3);
    return std::make_pair(BT_Unknown, 0);
  }

  // It could still be utf-32 or utf-16.
  if (input.size() >= 4 && input[1] == 0 && input[2] == 0 && input[3] == 0)
    return std::make_pair(BT_UTF32_LE, 0);

  if (input.size() >= 2 && input[1] == 0)
    return std::make_pair(BT_UTF16_LE, 0);

  return std::make_pair(BT_UTF8, 0);
}

struct StreamStartInfo {
  BOMType BOM;
};

struct VersionDirectiveInfo {
  StringRef Value;
};

struct ScalarInfo {
  StringRef Value;
};

struct Token {
  enum TokenKind {
    TK_Null, // Uninitialized token.
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
    TK_Scalar
  } Kind;

  StringRef Range;

  union {
    StreamStartInfo StreamStart;
  };
  VersionDirectiveInfo VersionDirective;
  ScalarInfo Scalar;

  Token() : Kind(TK_Null) {}
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

class Scanner {
  SourceMgr *SM;
  OwningPtr<MemoryBuffer> InputBuffer;
  StringRef::iterator Cur;
  StringRef::iterator End;
  int Indent; //< Current YAML indentation level in spaces.
  unsigned Column; //< Current column number in unicode code points.
  unsigned Line; //< Current line number.
  unsigned FlowLevel;
  unsigned BlockLevel;
  bool IsStartOfStream;
  bool IsSimpleKeyAllowed;
  bool IsSimpleKeyRequired;
  std::deque<Token> TokenQueue;
  SmallVector<int, 4> Indents;
  SmallVector<SimpleKey, 4> SimpleKeys;


  StringRef currentInput() {
    return StringRef(Cur, End - Cur);
  }

  bool isAtEnd(StringRef::iterator i = 0) {
    if (i)
      return i == End;
    return Cur == End;
  }

  typedef std::pair<uint32_t, unsigned> UTF8Decoded;

  UTF8Decoded decodeUTF8(StringRef::iterator Pos) {
    if ((*Pos & 0x80) == 0)
      return std::make_pair(*Pos, 1);

    if (   (*Pos & 0xE0) == 0xC0
        && Pos + 1 != End
        && *Pos >= 0xC2
        && *Pos <= 0xDF
        && *(Pos + 1) >= 0x80
        && *(Pos + 1) <= 0xBF) {
      uint32_t codepoint = *(Pos + 1) & 0x3F;
      codepoint |= uint16_t((*Pos) & 0x1F) << 6;
      return std::make_pair(codepoint, 2);
    }

    if (   (*Pos & 0xF0) == 0xE0
        && Pos + 2 < End) {
      if (!(   *Pos == 0xE0
            && *(Pos + 1) >= 0xA0
            && *(Pos + 1) <= 0xBF));
      else if (!(   *Pos >= 0xE1
                 && *Pos <= 0xEC
                 && *(Pos + 1) >= 0x80
                 && *(Pos + 1) <= 0xBF));
      else if (!(   *Pos == 0xED
                 && *(Pos + 1) >= 0x80
                 && *(Pos + 1) <= 0x9F));
      else if (!(   *Pos >= 0xEE
                 && *Pos <= 0xEF
                 && *(Pos + 1) >= 0x80
                 && *(Pos + 1) <= 0xBF));
      else {
        if (*(Pos + 2) >= 0x80 && *(Pos + 2) <= 0xBF) {
          uint32_t codepoint = *(Pos + 2) & 0x3F;
          codepoint |= uint16_t(*(Pos + 1) & 0x3F) << 6;
          codepoint |= uint16_t((*Pos) & 0x0F) << 12;
          return std::make_pair(codepoint, 3);
        }
      }
    }

    if (   (*Pos & 0xF8) == 0xF0
        && Pos + 3 < End) {
      if (!(   *Pos == 0xF0
            && *(Pos + 1) >= 0x90
            && *(Pos + 1) <= 0xBF));
      else if (!(   *Pos >= 0xF1
                 && *Pos <= 0xF3
                 && *(Pos + 1) >= 0x80
                 && *(Pos + 1) <= 0xBF));
      else if (!(   *Pos == 0xF4
                 && *(Pos + 1) >= 0x80
                 && *(Pos + 1) <= 0x8F));
      else {
        if (   *(Pos + 2) >= 0x80 && *(Pos + 2) <= 0xBF
            && *(Pos + 3) >= 0x80 && *(Pos + 3) <= 0xBF) {
          uint32_t codepoint = *(Pos + 3) & 0x3F;
          codepoint |= uint32_t(*(Pos + 2) & 0x3F) << 6;
          codepoint |= uint32_t(*(Pos + 1) & 0x3F) << 12;
          codepoint |= uint32_t((*Pos) & 0x07) << 18;
          return std::make_pair(codepoint, 4);
        }
      }
    }

    // Not valid utf-8.
    report_fatal_error("Invalid utf-8!");
    return std::make_pair(0, 0);
  }

  // Skip valid printable characters.
  StringRef::iterator skip_nb_char(StringRef::iterator Pos) {
    // Check 7 bit c-printable - b-char.
    if (   *Pos == 0x09
        || (*Pos >= 0x20 && *Pos <= 0x7E))
      return Pos + 1;

    // Check for valid utf-8.
    if (*Pos & 0x80) {
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

  StringRef::iterator skip_b_char(StringRef::iterator Pos) {
    if (*Pos == 0x0D) {
      if (Pos + 1 != End && *(Pos + 1) == 0x0A)
        return Pos + 2;
      return Pos + 1;
    }

    if (*Pos == 0x0A)
      return Pos + 1;
    return Pos;
  }

  StringRef::iterator skip_s_white(StringRef::iterator Pos) {
    if (Pos == End)
      return Pos;
    if (*Pos == ' ' || *Pos == '\t')
      return Pos + 1;
    return Pos;
  }

  StringRef::iterator skip_ns_char(StringRef::iterator Pos) {
    if (Pos == End)
      return Pos;
    if (*Pos == ' ' || *Pos == '\t')
      return Pos;
    return skip_nb_char(Pos);
  }

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

  StringRef scan_ns_plain_one_line() {
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

  bool consume(uint32_t expected) {
    if (expected >= 0x80)
      report_fatal_error("Not dealing with this yet");
    if (Cur == End)
      return false;
    if (*Cur >= 0x80)
      report_fatal_error("Not dealing with this yet");
    if (*Cur == expected) {
      ++Cur;
      ++Column;
      return true;
    }
    return false;
  }

  void skip(uint32_t Distance) {
    Cur += Distance;
    Column += Distance;
  }

  // Skip whitespace while keeping track of line number and column.
  void scanToNextToken() {
    while (true) {
      while (*Cur == ' ') {
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
      StringRef::iterator i = skip_b_char(Cur);
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

  bool isBlankOrBreak(StringRef::iterator Pos) {
    if (Pos == End)
      return false;
    if (*Pos == ' ' || *Pos == '\t' || *Pos == '\r' || *Pos == '\n')
      return true;
    return false;
  }

  void removeStaleSimpleKeys() {
    for (SmallVectorImpl<SimpleKey>::iterator i = SimpleKeys.begin();
                                              i != SimpleKeys.end();) {
      if (i->Line != Line || i->Column + 1024 < Column) {
        if (i->IsRequired)
          report_fatal_error("Could not find expected : for simple key");
        i = SimpleKeys.erase(i);
      } else
        ++i;
    }
  }

  void removeSimpleKeyOnFlowLevel(unsigned Level) {
    if (!SimpleKeys.empty() && (SimpleKeys.end() - 1)->FlowLevel == Level)
      SimpleKeys.pop_back();
  }

  bool scanStreamStart() {
    IsStartOfStream = false;

    BOMInfo BI = getBOM(currentInput());
    Cur += BI.second;

    Token t;
    t.Kind = Token::TK_StreamStart;
    t.StreamStart.BOM = BI.first;
    TokenQueue.push_back(t);
    return true;
  }

  bool scanStreamEnd() {
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

  bool unrollIndent(int Col) {
    Token t;
    // Indentation is ignored in flow.
    if (FlowLevel != 0)
      return true;

    while (Indent > Col) {
      t.Kind = Token::TK_BlockEnd;
      TokenQueue.push_back(t);
      Indent = Indents.pop_back_val();
    }

    return true;
  }

  bool rollIndent( int Col
                 , Token::TokenKind Kind
                 , std::deque<Token>::iterator InsertPoint) {
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

  bool scanDirective() {
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

  bool scanDocumentIndicator(bool IsStart) {
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

  bool scanFlowCollectionStart(bool IsSequence) {
    IsSimpleKeyAllowed = true;
    Token t;
    t.Kind = IsSequence ? Token::TK_FlowSequenceStart
                        : Token::TK_FlowMappingStart;
    t.Range = StringRef(Cur, 1);
    skip(1);
    TokenQueue.push_back(t);
    ++FlowLevel;
    return true;
  }

  bool scanFlowCollectionEnd(bool IsSequence) {
    removeSimpleKeyOnFlowLevel(FlowLevel);
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

  bool scanFlowEntry() {
    removeSimpleKeyOnFlowLevel(FlowLevel);
    IsSimpleKeyAllowed = true;
    Token t;
    t.Kind = Token::TK_FlowEntry;
    t.Range = StringRef(Cur, 1);
    skip(1);
    TokenQueue.push_back(t);
    return true;
  }

  bool scanBlockEntry() {
    rollIndent(Column, Token::TK_BlockSequenceStart, TokenQueue.end());
    removeSimpleKeyOnFlowLevel(FlowLevel);
    IsSimpleKeyAllowed = true;
    Token t;
    t.Kind = Token::TK_BlockEntry;
    t.Range = StringRef(Cur, 1);
    skip(1);
    TokenQueue.push_back(t);
    return true;
  }

  bool scanKey() {
    if (!FlowLevel)
      rollIndent(Column, Token::TK_BlockMappingStart, TokenQueue.end());

    removeSimpleKeyOnFlowLevel(FlowLevel);
    IsSimpleKeyAllowed = !FlowLevel;

    Token t;
    t.Kind = Token::TK_Key;
    t.Range = StringRef(Cur, 1);
    skip(1);
    TokenQueue.push_back(t);
    return true;
  }

  bool scanValue() {
    // If the previous token could have been a simple key, insert the key token
    // into the token queue.
    if (!SimpleKeys.empty()) {
      SimpleKey SK = SimpleKeys.pop_back_val();
      Token t;
      t.Kind = Token::TK_Key;
      t.Range = SK.Tok->Range;
      std::deque<Token>::iterator i, e;
      for (i = TokenQueue.begin(), e = TokenQueue.end(); i != e; ++i) {
        if (&(*i) == SK.Tok)
          break;
      }
      assert(i != e && "SimpleKey not in token queue!");
      i = TokenQueue.insert(i, t);

      // We may also to add a Block-Mapping-Start token.
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

  bool scanFlowScalar(bool IsDoubleQuoted) {
    StringRef::iterator Start = Cur;
    unsigned ColStart = Column;
    skip(1); // eat quote.
    while (*Cur != (IsDoubleQuoted ? '"' : '\'')) {
      StringRef::iterator i = skip_nb_char(Cur);
      if (i == Cur)
        break;
      Cur = i;
      ++Column;
    }
    StringRef Value(Start + 1, Cur - (Start + 1));
    skip(1); // Skip ending quote.
    Token t;
    t.Kind = Token::TK_Scalar;
    t.Range = StringRef(Start, Cur - Start);
    t.Scalar.Value = Value;
    TokenQueue.push_back(t);

    if (IsSimpleKeyAllowed) {
      SimpleKey SK;
      SK.Tok = &TokenQueue.front();
      SK.Line = Line;
      SK.Column = ColStart;
      SK.IsRequired = false;
      SimpleKeys.push_back(SK);
    }

    IsSimpleKeyAllowed = false;

    return true;
  }

  bool scanPlainScalar() {
    StringRef::iterator Start = Cur;
    unsigned ColStart = Column;
    while (true) {
      if ((*Cur == ':' && isBlankOrBreak(Cur + 1))
          || (FlowLevel
              && StringRef(Cur, 1).find_first_of(",:?[]{}")
                 != StringRef::npos))
        break;
      StringRef::iterator i = skip_nb_char(Cur);
      if (i == Cur)
        break;
      Cur = i;
    }
    Token t;
    t.Kind = Token::TK_Scalar;
    t.Range = StringRef(Start, Cur - Start);
    t.Scalar.Value = t.Range;
    TokenQueue.push_back(t);

    // Plain scalars can be simple keys.
    if (IsSimpleKeyAllowed) {
      SimpleKey SK;
      SK.Tok = &TokenQueue.front();
      SK.Line = Line;
      SK.Column = ColStart;
      SK.IsRequired = false;
      SimpleKeys.push_back(SK);
    }

    IsSimpleKeyAllowed = false;

    return true;
  }

  bool fetchMoreTokens() {
    if (IsStartOfStream)
      return scanStreamStart();

    scanToNextToken();

    if (Cur == End)
      return scanStreamEnd();

    removeStaleSimpleKeys();

    unrollIndent(Column);

    if (Column == 0 && *Cur == '%')
      return scanDirective();

    if (Column == 0 && Cur + 4 < End
        && *Cur == '-'
        && *(Cur + 1) == '-'
        && *(Cur + 2) == '-'
        && (isBlankOrBreak(Cur + 3) || Cur + 3 == End))
      return scanDocumentIndicator(true);

    if (Column == 0 && Cur + 4 < End
        && *Cur == '.'
        && *(Cur + 1) == '.'
        && *(Cur + 2) == '.'
        && (isBlankOrBreak(Cur + 3) || Cur + 3 == End))
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
            && isBlankOrBreak(Cur + 1)))
      return scanPlainScalar();

    return false;
  }

public:
  Scanner(StringRef input, SourceMgr *sm)
    : SM(sm)
    , Indent(-1)
    , Column(0)
    , Line(0)
    , FlowLevel(0)
    , BlockLevel(0)
    , IsStartOfStream(true)
    , IsSimpleKeyAllowed(true)
    , IsSimpleKeyRequired(false) {
    InputBuffer.reset(MemoryBuffer::getMemBuffer(input, "YAML"));
    Cur = InputBuffer->getBufferStart();
    End = InputBuffer->getBufferEnd();
  }

  Token peekNext() {
    // If the current token is a possible simple key, keep parsing until we
    // can confirm.
    bool NeedMore = false;
    while (true) {
      if (TokenQueue.empty() || NeedMore)
        if (!fetchMoreTokens())
          report_fatal_error("Failed to get next token!");
      assert(!TokenQueue.empty() &&
             "fetchMoreTokens lied about getting tokens!");

      removeStaleSimpleKeys();
      SimpleKey SK;
      SK.Tok = &TokenQueue.front();
      if (std::find(SimpleKeys.begin(), SimpleKeys.end(), SK)
          == SimpleKeys.end())
        break;
      else
        NeedMore = true;
    }
    return TokenQueue.front();
  }

  Token getNext() {
    Token ret = peekNext();
    TokenQueue.pop_front();
    return ret;
  }
};

class document_iterator;
class Document;

class Stream {
  Scanner S;
  SourceMgr &SM;
  OwningPtr<Document> CurrentDoc;

  friend class Document;

  void handleYAMLDirective(const Token &t) {

  }

public:
  Stream(StringRef input, SourceMgr *sm) : S(input, sm), SM(*sm) {}

  document_iterator begin();
  document_iterator end();
};

class Node {
  unsigned int TypeID;

protected:
  Document *Doc;

public:
  enum NodeKind {
    NK_Null,
    NK_Scalar,
    NK_KeyValue,
    NK_Mapping,
    NK_Sequence
  };

  Node(unsigned int Type, Document *D) : TypeID(Type), Doc(D) {}

  unsigned int getType() const { return TypeID; }
  static inline bool classof(const Node *) { return true; }

  Token &peekNext();
  Token getNext();
  Node *parseBlockNode();

  virtual void skip() {}
};

class ScalarNode : public Node {
  StringRef Value;

public:
  ScalarNode(Document *D, StringRef Val) : Node(NK_Scalar, D), Value(Val) {}

  // Return Value without any escaping or folding or other fun YAML stuff. This
  // is the exact bytes that are contained in the file (after converstion to
  // utf8).
  StringRef getRawValue() const { return Value; }

  static inline bool classof(const ScalarNode *) { return true; }
  static inline bool classof(const Node *n) {
    return n->getType() == NK_Scalar;
  }
};

class KeyValueNode : public Node {
public:
  KeyValueNode(Document *D) : Node(NK_KeyValue, D) {}

  static inline bool classof(const KeyValueNode *) { return true; }
  static inline bool classof(const Node *n) {
    return n->getType() == NK_KeyValue;
  }
};

class MappingNode : public Node {
  Node *Key;

public:
  MappingNode(Document *D, Node *K) : Node(NK_Mapping, D), Key(K) {}

  static inline bool classof(const MappingNode *) { return true; }
  static inline bool classof(const Node *n) {
    return n->getType() == NK_Mapping;
  }
};

class SequenceNode : public Node {
  enum {
    ST_Block,
    ST_Flow
  } SequenceType;

public:
  class iterator {
    SequenceNode *SN;
    Node *CurrentEntry;

  public:
    iterator() : SN(0), CurrentEntry(0) {}
    iterator(SequenceNode *sn) : SN(sn), CurrentEntry(0) {}

    Node *operator ->() const {
      assert(CurrentEntry && "Attempted to access end iterator!");
      return CurrentEntry;
    }

    Node &operator *() const {
      assert(CurrentEntry && "Attempted to dereference end iterator!");
      return *CurrentEntry;
    }

    operator Node*() const {
      assert(CurrentEntry && "Attempted to access end iterator!");
      return CurrentEntry;
    }

    bool operator !=(const iterator &Other) const {
      return SN != Other.SN;
    }

    iterator &operator++() {
      assert(SN && "Attempted to advance iterator past end!");
      if (CurrentEntry)
        CurrentEntry->skip();
      Token t = SN->peekNext();
      switch (t.Kind) {
      case Token::TK_BlockEntry:
        SN->getNext();
        CurrentEntry = SN->parseBlockNode();
        break;
      case Token::TK_BlockEnd:
        SN->getNext();
        SN = 0;
        CurrentEntry = 0;
        break;
      default:
        report_fatal_error("Unexptected token!");
      }
      return *this;
    }
  };

  SequenceNode(Document *D, bool IsBlock)
    : Node(NK_Sequence, D)
    , SequenceType(IsBlock ? ST_Block : ST_Flow)
  {}

  static inline bool classof(const SequenceNode *) { return true; }
  static inline bool classof(const Node *n) {
    return n->getType() == NK_Sequence;
  }

  iterator begin() {
    iterator ret(this);
    ++ret;
    return ret;
  }

  iterator end() { return iterator(); }
};

class Document {
  friend Node;

  Stream &S;
  BumpPtrAllocator NodeAllocator;

  Token &peekNext() {
    return S.S.peekNext();
  }

  Token getNext() {
    return S.S.getNext();
  }

  void handleTagDirective(const Token &t) {

  }

  bool parseDirectives() {
    bool dir = false;
    while (true) {
      Token t = peekNext();
      if (t.Kind == Token::TK_TagDirective) {
        handleTagDirective(getNext());
        dir = true;
      } else if (t.Kind == Token::TK_VersionDirective) {
        S.handleYAMLDirective(getNext());
        dir = true;
      } else
        break;
    }
    return dir;
  }

  bool expectToken(Token::TokenKind TK) {
    Token t = getNext();
    if (t.Kind != TK)
      report_fatal_error("Unexpected token!");
    return true;
  }

public:
  Node *parseBlockNode() {
    Token t = getNext();
    switch (t.Kind) {
    case Token::TK_BlockSequenceStart:
      return new (NodeAllocator.Allocate<SequenceNode>(1))
        SequenceNode(this, true);
    case Token::TK_BlockMappingStart:
      break;
    case Token::TK_FlowSequenceStart:
      return new (NodeAllocator.Allocate<SequenceNode>(1))
        SequenceNode(this, false);
    case Token::TK_FlowMappingStart:
      break;
    case Token::TK_Scalar:
      return new (NodeAllocator.Allocate<ScalarNode>(1))
        ScalarNode(this, t.Scalar.Value);
    default:
      report_fatal_error("Unexpected token!");
    }
    assert(false && "Control flow shouldn't reach here.");
    return 0;
  }

  Document(Stream &s) : S(s) {
    if (parseDirectives())
      expectToken(Token::TK_DocumentStart);
  }

  bool skip() {
    return false;
  }

  Node *getRoot() {
    return parseBlockNode();
  }
};

class document_iterator {
  Document *Doc;

public:
  document_iterator() : Doc(0) {}
  document_iterator(Document *d) : Doc(d) {}

  bool operator !=(const document_iterator &other) {
    return Doc != other.Doc;
  }

  document_iterator operator ++() {
    if (!Doc->skip())
      Doc = 0;
    return *this;
  }

  Document *operator ->() {
    return Doc;
  }
};

document_iterator Stream::begin() {
  if (CurrentDoc)
    report_fatal_error("Can only iterate over the stream once");

  // Skip Stream-Start.
  S.getNext();

  CurrentDoc.reset(new Document(*this));
  return document_iterator(CurrentDoc.get());
}

document_iterator Stream::end() {
  return document_iterator();
}

Token &Node::peekNext() {
  return Doc->peekNext();
}

Token Node::getNext() {
  return Doc->getNext();
}

Node *Node::parseBlockNode() {
  return Doc->parseBlockNode();
}

} // end namespace yaml.
} // end namespace llvm.

using namespace llvm;

struct indent {
  unsigned distance;
  indent(unsigned d) : distance(d) {}
};

raw_ostream &operator <<(raw_ostream &os, const indent &in) {
  for (unsigned i = 0; i < in.distance; ++i)
    os << "  ";
  return os;
}

void dumpNode(yaml::Node *n, unsigned Indent = 0) {
  if (yaml::ScalarNode *sn = dyn_cast<yaml::ScalarNode>(n))
    outs() << indent(Indent) << "!!str \"" << sn->getRawValue() << "\"\n";
  else if (yaml::SequenceNode *sn = dyn_cast<yaml::SequenceNode>(n)) {
    outs() << indent(Indent) << "!!seq [\n";
    ++Indent;
    for (yaml::SequenceNode::iterator i = sn->begin(), e = sn->end();
                                      i != e; ++i) {
      dumpNode(i, Indent);
    }
    --Indent;
    outs() << indent(Indent) << "]\n";
  }
}

int main(int argc, char **argv) {
  // llvm::cl::ParseCommandLineOptions(argc, argv);
  llvm::SourceMgr sm;

  // How do I want to use yaml...
  OwningPtr<MemoryBuffer> Buf;
  error_code ec = MemoryBuffer::getFileOrSTDIN(argv[1], Buf);

  yaml::Scanner s(Buf->getBuffer(), &sm);

  while (true) {
    yaml::Token t = s.getNext();
    switch (t.Kind) {
    case yaml::Token::TK_StreamStart:
      outs() << "Stream-Start(" << t.StreamStart.BOM << "): ";
      break;
    case yaml::Token::TK_StreamEnd:
      outs() << "Stream-End: ";
      break;
    case yaml::Token::TK_VersionDirective:
      outs() << "Version-Directive(" << t.VersionDirective.Value << "): ";
      break;
    case yaml::Token::TK_TagDirective:
      outs() << "Tag-Directive: ";
      break;
    case yaml::Token::TK_DocumentStart:
      outs() << "Document-Start: ";
      break;
    case yaml::Token::TK_DocumentEnd:
      outs() << "Document-End: ";
      break;
    case yaml::Token::TK_BlockEntry:
      outs() << "Block-Entry: ";
      break;
    case yaml::Token::TK_BlockEnd:
      outs() << "Block-End: ";
      break;
    case yaml::Token::TK_BlockSequenceStart:
      outs() << "Block-Sequence-Start: ";
      break;
    case yaml::Token::TK_BlockMappingStart:
      outs() << "Block-Mapping-Start: ";
      break;
    case yaml::Token::TK_FlowEntry:
      outs() << "Flow-Entry: ";
      break;
    case yaml::Token::TK_FlowSequenceStart:
      outs() << "Flow-Sequence-Start: ";
      break;
    case yaml::Token::TK_FlowSequenceEnd:
      outs() << "Flow-Sequence-End: ";
      break;
    case yaml::Token::TK_FlowMappingStart:
      outs() << "Flow-Mapping-Start: ";
      break;
    case yaml::Token::TK_FlowMappingEnd:
      outs() << "Flow-Mapping-End: ";
      break;
    case yaml::Token::TK_Key:
      outs() << "Key: ";
      break;
    case yaml::Token::TK_Value:
      outs() << "Value: ";
      break;
    case yaml::Token::TK_Scalar:
      outs() << "Scalar(" << t.Scalar.Value << "): ";
      break;
    }
    outs() << t.Range << "\n";
    if (t.Kind == yaml::Token::TK_StreamEnd)
      break;
    outs().flush();
  }

  yaml::Stream stream(Buf->getBuffer(), &sm);
  for (yaml::document_iterator di = stream.begin(), de = stream.end(); di != de;
       ++di) {
    outs() << "%YAML 1.2\n"
           << "---\n";
    dumpNode(di->getRoot());
    outs() << "...\n";
  }

  if (argc < 2) {
    errs() << "Not enough args.\n";
    return 1;
  }

  return 0;
}
