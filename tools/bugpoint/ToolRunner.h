//===-- tools/bugpoint/ToolRunner.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file exposes an abstraction around a platform C compiler, used to
// compile C and assembly code.  It also exposes an "AbstractIntepreter"
// interface, which is used to execute code using one of the LLVM execution
// engines.
//
//===----------------------------------------------------------------------===//

#ifndef BUGPOINT_TOOLRUNNER_H
#define BUGPOINT_TOOLRUNNER_H

#include "Failure.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/Path.h"
#include "llvm/System/system_error.h"
#include <exception>
#include <vector>

namespace llvm {

extern cl::opt<bool> SaveTemps;
extern Triple TargetTriple;

class CBE;
class LLC;

/// @brief A generic compiler argument.
struct CompilerArgument {
  struct /* enum class */ FileType {
    enum _ {
      Invalid,
      Asm,
      C,
      Executable,
      Object,
      SharedObject,
    };
  };

  struct /* enum class */ ArgumentType {
    enum _ {
      InputFileType,  ///< Type or language of input file. (gcc: -x)
      InputFilePath,  ///< Path to input file. (gcc: <positional>)
      /// Type of output file in [Executable, Object, SharedObject]
      OutputFileType,
      OutputFilePath  ///< Path to output file. (gcc: -o)
    };
  };

  ArgumentType ArgType;
  sys::Path Path; ///< Silly C++, POD types are for C!
  union {
    FileType::_ InputFileType;
    FileType::_ OutputFileType;
  };
};

/// @brief Abstract interface to a C compiler.
class CCompiler {
public:
  // Public API.
  enum Compilers {
    GCCCompatible,
    MicrosoftC
  };

  // Typedefs.
  typedef SmallVectorImpl<CompilerArgument> ArgumentList;
  typedef SmallVectorImpl<StringRef>    UserArgumentList;

  // No public constructor.
  virtual ~CCompiler();

  /// @param CompilerType The compiler in enum Compilers to create.
  /// @param ExecutablePath The path to the executable to use to compile. This
  ///        must be compatable with the CompilerType (don't pass gcc as cl.exe)
  /// @param Args Compiler independent default arguments. These are used for all
  ///        invocations.
  /// @param UserArgs Compiler specific arguments passed by the user on the
  ///        command line. These are used for all invocations.
  ///
  /// @return A pointer to the compiler.
  static CCompiler* createCCompiler(Compilers CompilerType,
                                    const sys::Path &ExecutablePath,
                                    const ArgumentList &Args,
                                    const UserArgumentList &UserArgs);

  /// @name Abstract CCompiler Interface
  /// @{

  /// Compile the given program, then execute it.
  ///
  /// This function cleans up all temporary files generated internally. This
  /// does _not_ include STD{Input,Output} or any input files passed via
  /// @p CompileArgs.
  ///
  /// @param CompileArgs The list of generic compiler options to compile with.
  /// @param ExecuteArgs The list of user supplied arguments to pass to the
  ///        compiled program when it is run.
  /// @param STDInput Path to file (or device) to read input from.
  /// @param STDOutput Path to file (or device) to send stdout and stderr to.
  /// @param Timeout Max time to let any program run. Reset at the start of
  ///        each step in [compile, link, execute].
  /// @param MemoryLimit Max memory any process is allowed to use (in MiB).
  /// @param ExitCode Return value of the last executed program. If the function
  ///        returns true this is the return value of the final program (which
  ///        may not be 0). If the return value is false, look at Failures to
  ///        determine which step failed.
  /// @param Failures Empty if the function returns true. Otherwise it is the
  ///        chain of failure information. If the last (highest index) entry is
  ///        a program_under_test_failure, the @p ExitCode contains the value
  ///        that program returned. Otherwise, if the function returned false,
  ///        ExitCode is undefined.
  ///
  ///
  /// @return true - Everything went dandy.
  ///         false - Something went wrong, take a look at the FailureChain.
  virtual bool CompileAndExecuteProgram(// Compile options.
                                        const ArgumentList &CompileArgs,
                                        // Execute options.
                                        const UserArgumentList &ExecuteArgs,
                                        const sys::Path &STDInput,
                                        const sys::Path &STDOutput,
                                        unsigned Timeout,
                                        unsigned MemoryLimit,
                                        int &ExitCode,
                                        // Error options.
                                        FailureChain &Failures);

  /// Compile the given program to the requested output type.
  ///
  /// @param CompileArgs The list of generic compiler options to compile with.
  ///        the input and output files are pulled from this list.
  /// @param ActualOutputFilePath The actual output file path. This will most
  ///        likely be different from what was requested due file name uniquing.
  /// @param Failures Empty if the function returns true. Otherwise it is the
  ///        chain of failure information. If the last (highest index) entry is
  ///        a program_under_test_failure, the @p ExitCode contains the value
  ///        that program returned. Otherwise, if the function returned false,
  ///        ExitCode is undefined.
  ///
  /// @return true - The compiler dutifully accomplished its masters wishes.
  ///         false - Some operation failed. Look at Failures for more info.
  virtual bool CompileProgram(const ArgumentList &CompileArgs,
                              sys::Path &ActualOutputFilePath,
                              FailureChain &Failures);
  /// @}


protected:
  // Constructor.
  CCompiler(const sys::Path &executablePath,
            const sys::Path &remoteClientPath,
            const ArgumentList &arguments,
            const UserArgumentList &userArguments)
  : ExecutablePath(executablePath)
  , RemoteClientPath(remoteClientPath)
  , Arguments(arguments.begin(), arguments.end())
  , UserArguments(userArguments.begin(), userArguments.end())
  {}

  // State.
  typedef SmallVector<CompilerArgument, 8> ArgumentList_t;
  typedef SmallVector<std::string, 2>  UserArgumentList_t;
  sys::Path ExecutablePath;   ///< The path to the compiler executable.
  sys::Path RemoteClientPath; ///< The path to the rsh / ssh executable.
  ArgumentList_t     Arguments;     ///< List of compiler independent arguments.
  UserArgumentList_t UserArguments; ///< List of compiler specific arguments.

private:
  // Noncopyable.
  CCompiler(const CCompiler &);
  CCompiler& operator=(const CCompiler &);
};

//===---------------------------------------------------------------------===//
/// AbstractInterpreter Class - Subclasses of this class are used to execute
/// LLVM bitcode in a variety of ways.  This abstract interface hides this
/// complexity behind a simple interface.
///
class AbstractInterpreter {
public:
  static CBE *createCBE(const char                     *Argv0,
                        FailureChain                   &Failures,
                        const sys::Path                &CompilerBinary,
                        const std::vector<std::string> *Args = 0,
                        const std::vector<std::string> *GCCArgs = 0);

  static LLC *createLLC(const char                     *Argv0,
                        FailureChain                   &Failures,
                        const std::string              &GCCBinary,
                        const std::vector<std::string> *Args = 0,
                        const std::vector<std::string> *GCCArgs = 0,
                        bool UseIntegratedAssembler = false);

  static AbstractInterpreter* createLLI(const char   *Argv0,
                                        FailureChain &Failures,
                                        const std::vector<std::string> *Args=0);

  static AbstractInterpreter* createJIT(const char *Argv0,
                                        FailureChain &Failures,
                                        const std::vector<std::string> *Args=0);

  static AbstractInterpreter* createCustom(StringRef ExecCommandLine,
                                           FailureChain &Failures);


  virtual ~AbstractInterpreter() {}

  /// compileProgram - Compile the specified program from bitcode to executable
  /// code.  This does not produce any output, it is only used when debugging
  /// the code generator.  It returns false if the code generator fails.
  // FIXME: [error-handling]
  virtual void compileProgram(const std::string &Bitcode,
                              FailureChain &Failures,
                              unsigned Timeout = 0,
                              unsigned MemoryLimit = 0) {}

  /// OutputCode - Compile the specified program from bitcode to code
  /// understood by the GCC driver (either C or asm).  If the code generator
  /// fails, it sets Error, otherwise, this function returns the type of code
  /// emitted.
  // FIXME: [error-handling]
  virtual CompilerArgument::FileType::_ OutputCode(const std::string &Bitcode,
                                                   sys::Path &OutFile,
                                                   FailureChain &Failures,
                                                   unsigned Timeout = 0,
                                                   unsigned MemoryLimit = 0) {
    Error = "OutputCode not supported by this AbstractInterpreter!";
    return CompilerArgument::FileType::Invalid;
  }

  /// ExecuteProgram - Run the specified bitcode file, emitting output to the
  /// specified filename.  This sets RetVal to the exit code of the program or
  /// returns false if a problem was encountered that prevented execution of
  /// the program.
  ///
  // FIXME: [error-handling]
  virtual int ExecuteProgram(const std::string &Bitcode,
                             const std::vector<std::string> &Args,
                             const std::string &InputFile,
                             const std::string &OutputFile,
                             FailureChain &Failures,
                             const std::vector<std::string> &GCCArgs =
                               std::vector<std::string>(),
                             const std::vector<std::string> &SharedLibs =
                               std::vector<std::string>(),
                             unsigned Timeout = 0,
                             unsigned MemoryLimit = 0) = 0;
};

//===---------------------------------------------------------------------===//
// CBE Implementation of AbstractIntepreter interface
//
class CBE : public AbstractInterpreter {
  sys::Path LLCPath;                 // The path to the `llc' executable.
  std::vector<std::string> ToolArgs; // Extra args to pass to LLC.
  CCompiler *Compiler;
public:
  CBE(const sys::Path &llcPath, CCompiler *compiler,
      const std::vector<std::string> *Args)
    : LLCPath(llcPath), Compiler(compiler) {
    ToolArgs.clear ();
    if (Args) ToolArgs = *Args;
  }
  ~CBE() { delete Compiler; }

  /// compileProgram - Compile the specified program from bitcode to executable
  /// code.  This does not produce any output, it is only used when debugging
  /// the code generator.  Returns false if the code generator fails.
  // FIXME: [error-handling]
  virtual void compileProgram(const std::string &Bitcode, std::string *Error,
                              unsigned Timeout = 0, unsigned MemoryLimit = 0);

  // FIXME: [error-handling]
  virtual int ExecuteProgram(const std::string &Bitcode,
                             const std::vector<std::string> &Args,
                             const std::string &InputFile,
                             const std::string &OutputFile,
                             FailureChain &Failures,
                             const std::vector<std::string> &GCCArgs =
                               std::vector<std::string>(),
                             const std::vector<std::string> &SharedLibs =
                               std::vector<std::string>(),
                             unsigned Timeout = 0,
                             unsigned MemoryLimit = 0);

  /// OutputCode - Compile the specified program from bitcode to code
  /// understood by the GCC driver (either C or asm).  If the code generator
  /// fails, it sets Error, otherwise, this function returns the type of code
  /// emitted.
  // FIXME: [error-handling]
  virtual CompilerArgument::FileType::_ OutputCode(const std::string &Bitcode,
                                                   sys::Path &OutFile,
                                                   FailureChain &Failures,
                                                   unsigned Timeout = 0,
                                                   unsigned MemoryLimit = 0);
};


//===---------------------------------------------------------------------===//
// LLC Implementation of AbstractIntepreter interface
//
class LLC : public AbstractInterpreter {
  std::string LLCPath;               // The path to the LLC executable.
  std::vector<std::string> ToolArgs; // Extra args to pass to LLC.
  CCompiler *Compiler;
  bool UseIntegratedAssembler;
public:
  LLC(const std::string &llcPath, CCompiler *compiler,
      const std::vector<std::string> *Args,
      bool useIntegratedAssembler,
      bool linkIntegratedAssemblerOutput)
    : LLCPath(llcPath), Compiler(compiler),
      UseIntegratedAssembler(useIntegratedAssembler) {
    ToolArgs.clear();
    if (Args) ToolArgs = *Args;
  }
  ~LLC() { delete Compiler; }

  /// compileProgram - Compile the specified program from bitcode to executable
  /// code.  This does not produce any output, it is only used when debugging
  /// the code generator.  Returns false if the code generator fails.
  // FIXME: [error-handling]
  virtual void compileProgram(const std::string &Bitcode,
                              FailureChain &Failures,
                              unsigned Timeout = 0,
                              unsigned MemoryLimit = 0);

  // FIXME: [error-handling]
  virtual int ExecuteProgram(const std::string &Bitcode,
                             const std::vector<std::string> &Args,
                             const std::string &InputFile,
                             const std::string &OutputFile,
                             FailureChain &Failures,
                             const std::vector<std::string> &GCCArgs =
                               std::vector<std::string>(),
                             const std::vector<std::string> &SharedLibs =
                                std::vector<std::string>(),
                             unsigned Timeout = 0,
                             unsigned MemoryLimit = 0);

  /// OutputCode - Compile the specified program from bitcode to code
  /// understood by the GCC driver (either C or asm).  If the code generator
  /// fails, it sets Error, otherwise, this function returns the type of code
  /// emitted.
  // FIXME: [error-handling]
  virtual CompilerArgument::FileType::_  OutputCode(const std::string &Bitcode,
                                                    sys::Path &OutFile,
                                                    FailureChain &Failures,
                                                    unsigned Timeout = 0,
                                                    unsigned MemoryLimit = 0);
};

} // End llvm namespace

#endif
