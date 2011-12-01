//===-- lold.cpp - llvm object link editor ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a linker.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/COFF.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <sstream>
#include <vector>
using namespace llvm;
using namespace object;
using namespace support;

static std::string ToolName;

static cl::list<std::string>
  InputFilenames(cl::Positional,
                 cl::desc("<input object files>"),
                 cl::ZeroOrMore);

class FileReader;

class Target {
  /// @brief Create a FileReader for the file at path. This opens the file,
  ///        figures out what type it is, and constructs a file object which it
  ///        owns.
  virtual FileReader *createReader(Twine path) = 0;
};

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.

  cl::ParseCommandLineOptions(argc, argv, "LLVM Object Link Editor\n");
  ToolName = argv[0];

  return 0;
}
