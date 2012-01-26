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

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace llvm;
using namespace yaml;

EncodingInfo llvm::yaml::getUnicodeEncoding(StringRef input) {
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

Token Scanner::getNext() {
  Token ret = peekNext();
  // TokenQueue can be empty if there was an error getting the next token.
  if (!TokenQueue.empty())
    TokenQueue.pop_front();
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

void Scanner::removeStaleSimpleKeys() {
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

void Scanner::removeSimpleKeyOnFlowLevel(unsigned Level) {
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

void Scanner::scanToNextToken() {
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
  if (IsSimpleKeyAllowed) {
    SimpleKey SK;
    SK.Tok = &TokenQueue.back();
    SK.Line = Line;
    SK.Column = Column - 1;
    SK.IsRequired = false;
    SK.FlowLevel = FlowLevel;
    SimpleKeys.push_back(SK);
  }

  // And may also be followed by a simple key.
  IsSimpleKeyAllowed = true;
  ++FlowLevel;
  return true;
}

bool Scanner::scanFlowCollectionEnd(bool IsSequence) {
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

bool Scanner::scanFlowEntry() {
  removeSimpleKeyOnFlowLevel(FlowLevel);
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
  removeSimpleKeyOnFlowLevel(FlowLevel);
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

  removeSimpleKeyOnFlowLevel(FlowLevel);
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
    std::deque<Token>::iterator i, e;
    for (i = TokenQueue.begin(), e = TokenQueue.end(); i != e; ++i) {
      if (&(*i) == SK.Tok)
        break;
    }
    assert(i != e && "SimpleKey not in token queue!");
    i = TokenQueue.insert(i, t);

    // We may also need to add a Block-Mapping-Start token.
    rollIndent(SK.Column, Token::TK_BlockMappingStart, i);

    // FIXME: This clear is here because the above invalidates all the
    //        deque<Token>::iterators.
    SimpleKeys.clear();
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
  // We calulate the number of consecutive '\'s before the current position
  // by iterating backwards through our string.
  while (I >= First && *I == '\\') --I;
  // (Position - 1 - I) now contains the number of '\'s before the current
  // position. If it is odd, the character at 'Positon' was escaped.
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

  if (IsSimpleKeyAllowed) {
    SimpleKey SK;
    SK.Tok = &TokenQueue.back();
    SK.Line = Line;
    SK.Column = ColStart;
    SK.IsRequired = false;
    SK.FlowLevel = FlowLevel;
    SimpleKeys.push_back(SK);
  }

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
  if (IsSimpleKeyAllowed) {
    SimpleKey SK;
    SK.Tok = &TokenQueue.back();
    SK.Line = Line;
    SK.Column = ColStart;
    SK.IsRequired = false;
    SK.FlowLevel = FlowLevel;
    SimpleKeys.push_back(SK);
  }

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
  if (IsSimpleKeyAllowed) {
    SimpleKey SK;
    SK.Tok = &TokenQueue.back();
    SK.Line = Line;
    SK.Column = ColStart;
    SK.IsRequired = false;
    SK.FlowLevel = FlowLevel;
    SimpleKeys.push_back(SK);
  }

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
  if (IsSimpleKeyAllowed) {
    SimpleKey SK;
    SK.Tok = &TokenQueue.back();
    SK.Line = Line;
    SK.Column = ColStart;
    SK.IsRequired = false;
    SK.FlowLevel = FlowLevel;
    SimpleKeys.push_back(SK);
  }

  IsSimpleKeyAllowed = false;

  return true;
}

bool Scanner::fetchMoreTokens() {
  if (IsStartOfStream)
    return scanStreamStart();

  scanToNextToken();

  if (Cur == End)
    return scanStreamEnd();

  removeStaleSimpleKeys();

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

Stream::Stream(StringRef input, SourceMgr &sm) : S(input, sm) {}

void Stream::handleYAMLDirective(const Token &t) {

}

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

void Stream::skip() {
  for (document_iterator i = begin(), e = end(); i != e; ++i)
    i->skip();
}

Node::Node(unsigned int Type, Document *D, StringRef A)
  : TypeID(Type)
  , Anchor(A)
  , Doc(D)
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

MappingNode::iterator &MappingNode::iterator::operator++() {
  assert(MN && "Attempted to advance iterator past end!");
  if (MN->failed()) {
    MN->IsAtEnd = true;
    MN = 0;
    CurrentEntry = 0;
    return *this;
  }
  if (CurrentEntry) {
    CurrentEntry->skip();
    if (MN->MType == MT_Inline) {
      MN->IsAtEnd = true;
      MN = 0;
      CurrentEntry = 0;
      return *this;
    }
  }
  Token t = MN->peekNext();
  if (t.Kind == Token::TK_Key || t.Kind == Token::TK_Scalar) {
    // KeyValueNode eats the TK_Key. That way it can detect null keys.
    CurrentEntry = new (MN->getAllocator().Allocate<KeyValueNode>())
      KeyValueNode(MN->Doc);
  } else if (MN->MType == MT_Block) {
    switch (t.Kind) {
    case Token::TK_BlockEnd:
      MN->getNext();
      MN->IsAtEnd = true;
      MN = 0;
      CurrentEntry = 0;
      break;
    default:
      MN->setError("Unexpected token. Expected Key or Block End", t);
    case Token::TK_Error:
      MN->IsAtEnd = true;
      MN = 0;
      CurrentEntry = 0;
    }
  } else {
    switch (t.Kind) {
    case Token::TK_FlowEntry:
      // Eat the flow entry and recurse.
      MN->getNext();
      return ++(*this);
    case Token::TK_FlowMappingEnd:
      MN->getNext();
    case Token::TK_Error:
      // Set this to end iterator.
      MN->IsAtEnd = true;
      MN = 0;
      CurrentEntry = 0;
      break;
    default:
      MN->setError( "Unexpected token. Expected Key, Flow Entry, or Flow "
                    "Mapping End."
                  , t);
      MN->IsAtEnd = true;
      MN = 0;
      CurrentEntry = 0;
    }
  }
  return *this;
}

SequenceNode::iterator &SequenceNode::iterator::operator++() {
  assert(SN && "Attempted to advance iterator past end!");
  if (SN->failed()) {
    SN->IsAtEnd = true;
    SN = 0;
    CurrentEntry = 0;
    return *this;
  }
  if (CurrentEntry)
    CurrentEntry->skip();
  Token t = SN->peekNext();
  if (SN->SeqType == ST_Block) {
    switch (t.Kind) {
    case Token::TK_BlockEntry:
      SN->getNext();
      CurrentEntry = SN->parseBlockNode();
      if (CurrentEntry == 0) { // An error occured.
        SN->IsAtEnd = true;
        SN = 0;
        CurrentEntry = 0;
      }
      break;
    case Token::TK_BlockEnd:
      SN->getNext();
      SN->IsAtEnd = true;
      SN = 0;
      CurrentEntry = 0;
      break;
    default:
      SN->setError( "Unexpected token. Expected Block Entry or Block End."
                  , t);
    case Token::TK_Error:
      SN->IsAtEnd = true;
      SN = 0;
      CurrentEntry = 0;
    }
  } else if (SN->SeqType == ST_Indentless) {
    switch (t.Kind) {
    case Token::TK_BlockEntry:
      SN->getNext();
      CurrentEntry = SN->parseBlockNode();
      if (CurrentEntry == 0) { // An error occured.
        SN->IsAtEnd = true;
        SN = 0;
        CurrentEntry = 0;
      }
      break;
    default:
    case Token::TK_Error:
      SN->IsAtEnd = true;
      SN = 0;
      CurrentEntry = 0;
    }
  } else if (SN->SeqType == ST_Flow) {
    switch (t.Kind) {
    case Token::TK_FlowEntry:
      // Eat the flow entry and recurse.
      SN->getNext();
      return ++(*this);
    case Token::TK_FlowSequenceEnd:
      SN->getNext();
    case Token::TK_Error:
      // Set this to end iterator.
      SN->IsAtEnd = true;
      SN = 0;
      CurrentEntry = 0;
      break;
    default:
      // Otherwise it must be a flow entry.
      CurrentEntry = SN->parseBlockNode();
      if (!CurrentEntry) {
        SN->IsAtEnd = true;
        SN = 0;
      }
      break;
    }
  }
  return *this;
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
