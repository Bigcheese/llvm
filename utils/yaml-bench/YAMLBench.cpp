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


#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/system_error.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/YAMLParser.h"

using namespace llvm;

static cl::opt<bool>
  DumpTokens( "tokens"
            , cl::desc("Print the tokenization of the file.")
            , cl::init(false)
            );

static cl::opt<bool>
  DumpCanonical( "canonical"
               , cl::desc("Print the canonical YAML for this file.")
               , cl::init(false)
               );

static cl::opt<std::string>
 Input(cl::Positional, cl::desc("<input>"));

static cl::opt<bool>
  Verify( "verify"
        , cl::desc(
            "Run a quick verification useful for regression testing")
        , cl::init(false)
        );

struct indent {
  unsigned distance;
  indent(unsigned d) : distance(d) {}
};

raw_ostream &operator <<(raw_ostream &os, const indent &in) {
  for (unsigned i = 0; i < in.distance; ++i)
    os << "  ";
  return os;
}

void dumpNode( yaml::Node *n
             , unsigned Indent = 0
             , bool SuppressFirstIndent = false) {
  if (!n)
    return;
  if (!SuppressFirstIndent)
    outs() << indent(Indent);
  StringRef Anchor = n->getAnchor();
  if (!Anchor.empty())
    outs() << "&" << Anchor << " ";
  if (yaml::ScalarNode *sn = dyn_cast<yaml::ScalarNode>(n)) {
    outs() << "!!str \"" << sn->getRawValue() << "\"";
  } else if (yaml::SequenceNode *sn = dyn_cast<yaml::SequenceNode>(n)) {
    outs() << "!!seq [\n";
    ++Indent;
    for (yaml::SequenceNode::iterator i = sn->begin(), e = sn->end();
                                      i != e; ++i) {
      dumpNode(i, Indent);
      outs() << ",\n";
    }
    --Indent;
    outs() << indent(Indent) << "]";
  } else if (yaml::MappingNode *mn = dyn_cast<yaml::MappingNode>(n)) {
    outs() << "!!map {\n";
    ++Indent;
    for (yaml::MappingNode::iterator i = mn->begin(), e = mn->end();
                                     i != e; ++i) {
      outs() << indent(Indent) << "? ";
      dumpNode(i->getKey(), Indent, true);
      outs() << "\n";
      outs() << indent(Indent) << ": ";
      dumpNode(i->getValue(), Indent, true);
      outs() << ",\n";
    }
    --Indent;
    outs() << indent(Indent) << "}";
  } else if (yaml::AliasNode *an = dyn_cast<yaml::AliasNode>(n)){
    outs() << "*" << an->getName();
  } else if (dyn_cast<yaml::NullNode>(n)) {
    outs() << "!!null null";
  }
}

void dumpTokens(yaml::Scanner &s) {
  while (true) {
    yaml::Token t = s.getNext();
    switch (t.Kind) {
    case yaml::Token::TK_StreamStart:
      outs() << "Stream-Start(" << t.StreamStart.Encoding << "): ";
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
    case yaml::Token::TK_Alias:
      outs() << "Alias(" << t.Scalar.Value << "): ";
      break;
    case yaml::Token::TK_Anchor:
      outs() << "Anchor(" << t.Scalar.Value << "): ";
      break;
    case yaml::Token::TK_Tag:
      outs() << "Tag: ";
      break;
    case yaml::Token::TK_Error:
      break;
    }
    outs() << t.Range << "\n";
    if (t.Kind == yaml::Token::TK_StreamEnd || t.Kind == yaml::Token::TK_Error)
      break;
    outs().flush();
  }
}

void dumpStream(yaml::Stream &stream) {
  for (yaml::document_iterator di = stream.begin(), de = stream.end(); di != de;
       ++di) {
    outs() << "%YAML 1.2\n"
           << "---\n";
    yaml::Node *n = di->getRoot();
    if (n)
      dumpNode(n);
    else
      break;
    outs() << "\n...\n";
  }
}

void benchmark(llvm::TimerGroup &Group, llvm::StringRef Name,
               llvm::StringRef JSONText) {
  llvm::Timer BaseLine((Name + ": Loop").str(), Group);
  BaseLine.startTimer();
  char C = 0;
  for (llvm::StringRef::iterator I = JSONText.begin(),
                                 E = JSONText.end();
       I != E; ++I) { C += *I; }
  BaseLine.stopTimer();
  volatile char DontOptimizeOut = C; (void)DontOptimizeOut;

  llvm::Timer Tokenizing((Name + ": Tokenizing").str(), Group);
  Tokenizing.startTimer();
  {
    llvm::SourceMgr SM;
    llvm::yaml::Scanner scanner(JSONText, SM);
    for (;;) {
      llvm::yaml::Token t = scanner.getNext();
      if (  t.Kind == llvm::yaml::Token::TK_StreamEnd
         || t.Kind == llvm::yaml::Token::TK_Error)
        break;
    }
  }
  Tokenizing.stopTimer();

  llvm::Timer Parsing((Name + ": Parsing").str(), Group);
  Parsing.startTimer();
  {
    llvm::SourceMgr SM;
    llvm::yaml::Stream stream(JSONText, SM);
    stream.skip();
  }
  Parsing.stopTimer();
}

std::string createJSONText(size_t MemoryMB, unsigned ValueSize) {
  std::string JSONText;
  llvm::raw_string_ostream Stream(JSONText);
  Stream << "[\n";
  size_t MemoryBytes = MemoryMB * 1024 * 1024;
  while (JSONText.size() < MemoryBytes) {
    Stream << " {\n"
           << "  \"key1\": \"" << std::string(ValueSize, '*') << "\",\n"
           << "  \"key2\": \"" << std::string(ValueSize, '*') << "\",\n"
           << "  \"key3\": \"" << std::string(ValueSize, '*') << "\"\n"
           << " }";
    Stream.flush();
    if (JSONText.size() < MemoryBytes) Stream << ",";
    Stream << "\n";
  }
  Stream << "]\n";
  Stream.flush();
  return JSONText;
}

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);
  if (Input.getNumOccurrences()) {
    OwningPtr<MemoryBuffer> Buf;
    if (MemoryBuffer::getFileOrSTDIN(Input, Buf))
      return 1;

    llvm::SourceMgr sm;
    if (DumpTokens) {
      yaml::Scanner s(Buf->getBuffer(), sm);
      dumpTokens(s);
    }

    if (DumpCanonical) {
      yaml::Stream stream(Buf->getBuffer(), sm);
      dumpStream(stream);
    }
  }

  if (Verify) {
    llvm::TimerGroup Group("YAML parser benchmark");
    benchmark(Group, "Fast", createJSONText(10, 500));
  }

  return 0;
}
