//===-- tools/bugpoint/Failure.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file exposes an interface for representing different failures in
// bugpoint using system_error. It also contains a FailureChain class that
// represents the entier path the failure took from beginning to end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BUGPOINT_FAILURE_H
#define LLVM_BUGPOINT_FAILURE_H

#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/System/system_error.h"
#include <vector>

namespace llvm {
  // We need an error_category for bugpoint level errors (compile failed, link
  // failed, etc...). Then we need a way to chain these together so when a
  // failure occurs (say we lack permission to run the compiler) we can tell the
  // user WHY it failed. An error starting a tool process is _not_ a crash, it
  // is a bugpoint failure, and we should stop when it is encountered.

  /// @name Error Categories
  /// @{

  /// Category for errors that occur within bugpoint.
  ///
  /// This error_category represents errors that occur within bugpoint
  /// itself, not the code under test. Any error of this type will result in
  /// bugpoint exiting. Any output generated is invalid. An example would be
  /// the safe compiler reporting that it generated an executable, but when we
  /// try to run it, it cannot be found.
  struct /* enum class */ bugpoint_error {
    enum _ {
    };
  };

  /// Category for errors that occur in programs that bugpoint runs.
  ///
  /// This error_category represents errors that occur in the program under
  /// test. This is meant to contain high level failure reasons such as [crash,
  /// failed to compile, failed to link, user script returned non 0].
  struct /* enum class */ program_under_test_failure {
    enum _ {
      /// The program failed to compile.
      ///
      /// This only applies to assembly (LLVM IR and machine code) and c files.
      compilation,

      /// The program failed to link.
      ///
      /// Given an object file (ELF, MachO, COFF), the link failed.
      link,

      /// The program ran, but terminated abnormally.
      ///
      /// The program was compiled, and linked sucessfully, but crashed while
      /// executing. This includes asserts, segfaults, dynamic loader failures,
      /// etc...
      crashed
    };
  };

  /// @}

  /// This class represents all the information related to a single failure.
  ///
  /// This class should contain all of the information that went into a specific
  /// failure. It should contain enough information to let the user know what
  /// bugpoint was trying to do. This should _not_ contain any string
  /// descriptions of the error. Instead create a subclass that contains the raw
  /// data in the context of the failure.
  class Failure {
  public:
    virtual ~Failure();

    /// Returns a human readable string describing the failure. Does _NOT_
    /// include leading or trailing new line, capitalization, or punctuation.
    /// However, it may still contain embedded new lines.
    virtual std::string message() const = 0;
  };

  class SystemFailure : public Failure {
  public:
    virtual std::string message() const {
      return Error.message();
    }

    error_code Error;
  };

  class BugpointFailure : public Failure {
  public:
    virtual std::string message() const {};

    bugpoint_error Reason;
  };

  class ProgramUnderTestFailure : public Failure {
  public:
    virtual std::string message() const {};

    program_under_test_failure Reason;
    std::vector<SmallString<8> > CommandLine;
  };

  // Most failure chains should end up about 3 errors long. Containing: [The
  // operating system error_code, the program_under_test_failure, and possibly
  // the bugpoint_error].

  /// This class represents the path of a failure.
  ///
  /// This class represents the path of a failure. The first item (index 0) is
  /// gernally the operating system error_code that reported the error, and the
  /// last item is the higher level reason for failure.
  typedef SmallVector<OwningPtr<Failure>, 3> FailureChain;
}

#endif // LLVM_BUGPOINT_FAILURE_H
