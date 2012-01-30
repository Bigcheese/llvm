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
//    * Multi-line literal folding.
//    * Tag resolution.
//    * UTF-16.
//    * BOMs anywhere other than the first Unicode scalar value in the file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_YAML_PARSER_H
#define LLVM_SUPPORT_YAML_PARSER_H

#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/SourceMgr.h"

#include <utility>

namespace llvm {
class MemoryBuffer;
class SourceMgr;

namespace yaml {

enum UnicodeEncodingForm {
  UEF_UTF32_LE,
  UEF_UTF32_BE,
  UEF_UTF16_LE,
  UEF_UTF16_BE,
  UEF_UTF8,     //< UTF-8 or ascii.
  UEF_Unknown   //< Not a valid Unicode file.
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

  /// A string of length 0 or more who's begin() points to the logical location
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
struct ilist_sentinel_traits<yaml::Token> {
  yaml::Token *createSentinel() const {
    return &Sentinel;
  }
  static void destroySentinel(yaml::Token*) {}

  yaml::Token *provideInitialHead() const { return createSentinel(); }
  yaml::Token *ensureHead(yaml::Token*) const { return createSentinel(); }
  static void noteHead(yaml::Token*, yaml::Token*) {}

private:
  mutable yaml::Token Sentinel;
};

template<>
struct ilist_node_traits<yaml::Token> {
  yaml::Token *createNode(const yaml::Token &V) {
    return new (Alloc.Allocate<yaml::Token>()) yaml::Token(V);
  }
  static void deleteNode(yaml::Token *V) {}

  void addNodeToList(yaml::Token *) {}
  void removeNodeFromList(yaml::Token *) {}
  void transferNodesFromList(ilist_node_traits &    /*SrcTraits*/,
                             ilist_iterator<yaml::Token> /*first*/,
                             ilist_iterator<yaml::Token> /*last*/) {}

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
  void saveSimpleKey(TokenQueueT::iterator Tok, unsigned Col, bool IsRequired);

  /// @brief Remove simple keys that can no longer be valid simple keys.
  ///
  /// Invalid simple keys are not on the current line or are further than 1024
  /// columns back.
  void removeStaleSimpleKeys();

  /// @brief Remove all simple keys on FlowLevel \a Level.
  void removeSimpleKeyOnFlowLevel(unsigned Level);

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

class document_iterator;
class Document;

/// @brief This class represents a YAML stream potentially containing multiple
///        documents.
class Stream {
  Scanner S;
  OwningPtr<Document> CurrentDoc;

  friend class Document;

  /// @brief Validate a %YAML x.x directive.
  void handleYAMLDirective(const Token &t);

public:
  Stream(StringRef input, SourceMgr &sm);

  document_iterator begin();
  document_iterator end();
  void skip();
  bool failed() { return S.failed(); }
  bool validate() {
    skip();
    return !failed();
  }
};

/// @brief Abstract base class for all Nodes.
class Node {
  unsigned int TypeID;
  StringRef Anchor;

protected:
  Document *Doc;

public:
  enum NodeKind {
    NK_Null,
    NK_Scalar,
    NK_KeyValue,
    NK_Mapping,
    NK_Sequence,
    NK_Alias
  };

  Node(unsigned int Type, Document *D, StringRef A);
  virtual ~Node();

  /// @brief Get the value of the anchor attached to this node. If it does not
  ///        have one, getAnchor().size() will be 0.
  StringRef getAnchor() const { return Anchor; }

  unsigned int getType() const { return TypeID; }
  static inline bool classof(const Node *) { return true; }

  // These functions forward to Document and Scanner.
  Token &peekNext();
  Token getNext();
  Node *parseBlockNode();
  BumpPtrAllocator &getAllocator();
  void setError(const Twine &Msg, Token &Tok);
  bool failed() const;

  virtual void skip() {};
};

/// @brief A null value.
class NullNode : public Node {
public:
  NullNode(Document *D) : Node(NK_Null, D, StringRef()) {}

  static inline bool classof(const NullNode *) { return true; }
  static inline bool classof(const Node *n) {
    return n->getType() == NK_Null;
  }
};

/// @brief A scalar node is an opaque datum that can be presented as a
///        series of zero or more Unicode scalar values.
class ScalarNode : public Node {
  StringRef Value;

public:
  ScalarNode(Document *D, StringRef Anchor, StringRef Val)
    : Node(NK_Scalar, D, Anchor)
    , Value(Val)
  {}

  // Return Value without any escaping or folding or other fun YAML stuff. This
  // is the exact bytes that are contained in the file (after conversion to
  // utf8).
  StringRef getRawValue() const { return Value; }

  static inline bool classof(const ScalarNode *) { return true; }
  static inline bool classof(const Node *n) {
    return n->getType() == NK_Scalar;
  }
};

/// @brief A key and value pair. While not technically a Node under the YAML
///        representation graph, it is easier to treat them this way.
///
/// TODO: Consider making this not a child of Node.
class KeyValueNode : public Node {
  Node *Key;
  Node *Value;

public:
  KeyValueNode(Document *D)
    : Node(NK_KeyValue, D, StringRef())
    , Key(0)
    , Value(0)
  {}

  static inline bool classof(const KeyValueNode *) { return true; }
  static inline bool classof(const Node *n) {
    return n->getType() == NK_KeyValue;
  }

  /// @brief Parse and return the key.
  Node *getKey();

  /// @brief Parse and return the value.
  Node *getValue();

  virtual void skip() {
    getKey()->skip();
    getValue()->skip();
  }
};

/// @brief This is an iterator abstraction over YAML collections shared by both
///        sequences and maps.
///
/// BaseT must have a ValueT* member named CurrentEntry and a member function
/// increment() which must set CurrentEntry to 0 to create an end iterator.
template <class BaseT, class ValueT>
class basic_collection_iterator
  : public std::iterator<std::forward_iterator_tag, ValueT> {
  BaseT *Base;

public:
  basic_collection_iterator() : Base(0) {}
  basic_collection_iterator(BaseT *B) : Base(B) {}

  ValueT *operator ->() const {
    assert(Base && Base->CurrentEntry && "Attempted to access end iterator!");
    return Base->CurrentEntry;
  }

  ValueT &operator *() const {
    assert(Base && Base->CurrentEntry &&
           "Attempted to dereference end iterator!");
    return *Base->CurrentEntry;
  }

  operator ValueT*() const {
    assert(Base && Base->CurrentEntry && "Attempted to access end iterator!");
    return Base->CurrentEntry;
  }

  bool operator !=(const basic_collection_iterator &Other) const {
    if(Base != Other.Base)
      return true;
    if ((Base && Other.Base) && Base->CurrentEntry != Other.Base->CurrentEntry)
      return true;
    return false;
  }

  basic_collection_iterator &operator++() {
    assert(Base && "Attempted to advance iterator past end!");
    Base->increment();
    // Create an end iterator.
    if (Base->CurrentEntry == 0)
      Base = 0;
    return *this;
  }
};

/// @brief Represents a YAML map created from either a block map for a flow map.
///
/// This parses the YAML stream as increment() is called.
class MappingNode : public Node {
public:
  enum Type {
    MT_Block,
    MT_Flow,
    MT_Inline //< An inline mapping node is used for "[key: value]".
  };

private:
  Type MType;
  bool IsAtBeginning;
  bool IsAtEnd;
  KeyValueNode *CurrentEntry;

  void increment();

public:
  MappingNode(Document *D, StringRef Anchor, Type T)
    : Node(NK_Mapping, D, Anchor)
    , MType(T)
    , IsAtBeginning(true)
    , IsAtEnd(false)
    , CurrentEntry(0)
  {}

  static inline bool classof(const MappingNode *) { return true; }
  static inline bool classof(const Node *n) {
    return n->getType() == NK_Mapping;
  }

  friend class basic_collection_iterator<MappingNode, KeyValueNode>;
  typedef basic_collection_iterator<MappingNode, KeyValueNode> iterator;

  iterator begin() {
    assert(IsAtBeginning && "You may only iterate over a collection once!");
    IsAtBeginning = false;
    iterator ret(this);
    ++ret;
    return ret;
  }

  iterator end() { return iterator(); }

  virtual void skip() {
    // TODO: support skipping from the middle of a parsed map ;/
    assert((IsAtBeginning || IsAtEnd) && "Cannot skip mid parse!");
    if (IsAtBeginning)
      for (iterator i = begin(), e = end(); i != e; ++i)
        i->skip();
  }
};

/// @brief Represents a YAML sequence created from either a block sequence for a
///        flow sequence.
///
/// This parses the YAML stream as increment() is called.
class SequenceNode : public Node {
public:
  enum Type {
    ST_Block,
    ST_Flow,
    // Use for:
    //
    // key:
    // - val1
    // - val2
    //
    // As a BlockMappingEntry and BlockEnd are not created in this case.
    ST_Indentless
  };

private:
  Type SeqType;
  bool IsAtBeginning;
  bool IsAtEnd;
  bool WasPreviousTokenFlowEntry;
  Node *CurrentEntry;

public:
  SequenceNode(Document *D, StringRef Anchor, Type T)
    : Node(NK_Sequence, D, Anchor)
    , SeqType(T)
    , IsAtBeginning(true)
    , IsAtEnd(false)
    , WasPreviousTokenFlowEntry(true) // Start with an imaginary ','.
    , CurrentEntry(0)
  {}

  static inline bool classof(const SequenceNode *) { return true; }
  static inline bool classof(const Node *n) {
    return n->getType() == NK_Sequence;
  }

  friend class basic_collection_iterator<SequenceNode, Node>;
  typedef basic_collection_iterator<SequenceNode, Node> iterator;

  void increment();

  iterator begin() {
    assert(IsAtBeginning && "You may only iterate over a collection once!");
    IsAtBeginning = false;
    iterator ret(this);
    ++ret;
    return ret;
  }

  iterator end() { return iterator(); }

  virtual void skip() {
    // TODO: support skipping from the middle of a parsed sequence ;/
    assert((IsAtBeginning || IsAtEnd) && "Cannot skip mid parse!");
    if (IsAtBeginning)
      for (iterator i = begin(), e = end(); i != e; ++i)
        i->skip();
  }
};

/// @brief Represents an alias to a Node with an anchor.
class AliasNode : public Node {
  StringRef Name;

public:
  AliasNode(Document *D, StringRef Val)
    : Node(NK_Alias, D, StringRef()), Name(Val) {}

  StringRef getName() const { return Name; }
  Node *getTarget();

  static inline bool classof(const ScalarNode *) { return true; }
  static inline bool classof(const Node *n) {
    return n->getType() == NK_Alias;
  }
};

/// @brief A YAML Stream is a sequence of Documents. A document contains a root
///        node.
class Document {
  friend class Node;
  friend class document_iterator;

  /// @brief Stream to read tokens from.
  Stream &S;

  /// @brief Used to allocate nodes to. All are destroyed without calling their
  ///        destructor when the document is destroyed.
  BumpPtrAllocator NodeAllocator;

  /// @brief The root node. Used to support skipping a partially parsed
  ///        document.
  Node *Root;

  Token &peekNext() {
    return S.S.peekNext();
  }

  Token getNext() {
    return S.S.getNext();
  }

  void setError(const Twine &Msg, Token &Tok) {
    S.S.setError(Msg, Tok.Range.begin());
  }

  bool failed() const {
    return S.S.failed();
  }

  void handleTagDirective(const Token &t) {

  }

  /// @brief Parse %BLAH directives and return true if any were encountered.
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

  /// @brief Consume the next token and error if it is not \a TK.
  bool expectToken(Token::TokenKind TK) {
    Token t = getNext();
    if (t.Kind != TK) {
      setError("Unexpected token", t);
      return false;
    }
    return true;
  }

public:
  /// @brief Root for parsing a node. Returns a single node.
  Node *parseBlockNode();

  Document(Stream &s) : S(s), Root(0) {
    if (parseDirectives())
      expectToken(Token::TK_DocumentStart);
    Token &t = peekNext();
    if (t.Kind == Token::TK_DocumentStart)
      getNext();
  }

  /// @brief Finish parsing the current document and return true if there are
  ///        more. Return false otherwise.
  bool skip() {
    if (S.S.failed())
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

  /// @brief Parse and return the root level node.
  Node *getRoot() {
    if (Root)
      return Root;
    return Root = parseBlockNode();
  }
};

/// @brief Iterator abstraction for Documents over a Stream.
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
    else {
      // In place destruct and new are used here to avoid an unneeded
      // allocation.
      Doc->~Document();
      new (Doc) Document(Doc->S);
    }
    return *this;
  }

  Document *operator ->() {
    return Doc;
  }
};

}
}

#endif
