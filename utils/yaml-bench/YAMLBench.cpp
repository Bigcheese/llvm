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
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/SourceMgr.h"

#include <queue>
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
    TK_BlockSequenceEnd,
    TK_BlockMappingStart,
  } Kind;

  StringRef Range;

  union {
    StreamStartInfo StreamStart;
  };

  Token() : Kind(TK_Null) {}
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
  std::queue<Token> TokenQueue;
  SmallVector<int, 4> Indents;

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

  StringRef::iterator skip_ns_char(StringRef::iterator Pos) {
    if (Pos == End)
      return Pos;
    if (*Pos == ' ' || *Pos == '\t')
      return Pos;
    return skip_nb_char(Pos);
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

  // Skip whitespace while keeping track of line number and column.
  void scanToNextToken() {
    while (true) {
      while (*Cur == ' ') {
        ++Cur;
        ++Column;
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
    }
  }

  bool isBlankOrBreak(StringRef::iterator Pos) {
    if (Pos == End)
      return false;
    if (*Pos == ' ' || *Pos == '\t' || *Pos == '\r' || *Pos == '\n')
      return true;
    return false;
  }

  bool scanStreamStart() {
    IsStartOfStream = false;
    BOMInfo BI = getBOM(currentInput());
    Cur += BI.second;
    Token t;
    t.Kind = Token::TK_StreamStart;
    t.StreamStart.BOM = BI.first;
    TokenQueue.push(t);
    return true;
  }

  bool scanStreamEnd() {
    // Force an ending new line if one isn't present.
    if (Column != 0) {
      Column = 0;
      ++Line;
    }

    Token t;
    t.Kind = Token::TK_StreamEnd;
    t.Range = StringRef(Cur, 0);
    TokenQueue.push(t);
    return true;
  }

  bool unrollIndent(unsigned Col) {
    Token t;
    // Indentation is ignored in flow.
    if (FlowLevel != 0)
      return true;

    while (Indent > Col) {
      t.Kind = Token::TK_BlockEnd;
      TokenQueue.push(t);
      Indent = Indents.pop_back_val();
    }

    return true;
  }

  bool scanDirective() {
    // Reset the indentation level.
    unrollIndent(-1);


  }

  bool fetchMoreTokens() {
    if (IsStartOfStream)
      return scanStreamStart();

    if (Cur == End)
      return scanStreamEnd();

    scanToNextToken();

    unrollIndent(Column);

    if (Column == 0 && *Cur == '%')
      return scanDirective();
  }

public:
  Scanner(StringRef input, SourceMgr *sm)
    : SM(sm)
    , Indent(-1)
    , Column(0)
    , Line(0)
    , FlowLevel(0)
    , BlockLevel(0)
    , IsStartOfStream(true) {
    InputBuffer.reset(MemoryBuffer::getMemBuffer(input, "YAML"));
    Cur = InputBuffer->getBufferStart();
    End = InputBuffer->getBufferEnd();
  }

  Token getNext() {

  }

  void debugPrintRest() {
    outs() << currentInput() << "\n";
  }
};

} // end namespace yaml.
} // end namespace llvm.

using namespace llvm;

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);
  llvm::SourceMgr sm;

  // How do I want to use yaml...
  /*yaml::Stream stream("hello", &sm);
  for (yaml::document_iterator di = stream.begin(), de = stream.end(); di != de;
       ++di) {
    outs() << "Hey, I got a document!\n";
    yaml::Node *n = di->getRoot();
    if (yaml::ScalarNode *sn = dyn_cast<yaml::ScalarNode>(n))
      outs() << sn->getRawValue() << "\n";
    stream.debugPrintRest();
  }*/

  return 0;
}
