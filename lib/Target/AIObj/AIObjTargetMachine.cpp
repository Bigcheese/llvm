//===-- AIObjTargetMachine.cpp - Define TargetMachine for AIObj -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "AIObj.h"
#include "AIObjTargetMachine.h"
#include "llvm/PassManager.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

namespace llvm {
  MCStreamer *createAIObjAsmStreamer(MCContext &Ctx, formatted_raw_ostream &OS,
                                     bool isVerboseAsm, bool useLoc,
                                     bool useCFI, bool useDwarfDirectory,
                                     MCInstPrinter *InstPrint,
                                     MCCodeEmitter *CE,
                                     MCAsmBackend *MAB,
                                     bool ShowInst);
}

extern "C" void LLVMInitializeAIObjTarget() {
  // Register the target.
  RegisterTargetMachine<AIObjTargetMachine> X(TheAIObjTarget);
  TargetRegistry::RegisterAsmStreamer(TheAIObjTarget, createAIObjAsmStreamer);
}

/// AIObjTargetMachine ctor - Create an ILP32 architecture model
///
AIObjTargetMachine::AIObjTargetMachine( const Target &T
                                      , StringRef TT
                                      , StringRef CPU
                                      , StringRef FS
                                      , const TargetOptions &Options
                                      , Reloc::Model RM
                                      , CodeModel::Model CM
                                      , CodeGenOpt::Level OL)
  : LLVMTargetMachine( T
                     , TT
                     , CPU
                     , FS
                     , Options
                     , RM
                     , CM
                     , OL)
  , Subtarget(TT, CPU, FS)
  , DataLayout(Subtarget.getDataLayout())
  , TLInfo(*this)
  , TSInfo(*this)
  , InstrInfo(*this)
  , FrameLowering(Subtarget) {
}

namespace {
/// AIObj Code Generator Pass Configuration Options.
class AIObjPassConfig : public TargetPassConfig {
public:
  AIObjPassConfig(AIObjTargetMachine *TM, PassManagerBase &PM)
    : TargetPassConfig(TM, PM) {}

  AIObjTargetMachine &getAIObjTargetMachine() const {
    return getTM<AIObjTargetMachine>();
  }

  virtual bool addInstSelector();
  FunctionPass *createTargetRegisterAllocator(bool);
  void addOptimizedRegAlloc(FunctionPass *RegAllocPass);
  bool addPostRegAlloc();
  virtual bool addPreEmitPass();
};
} // namespace

TargetPassConfig *AIObjTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new AIObjPassConfig(this, PM);
}

bool AIObjPassConfig::addInstSelector() {
  PM.add(createAIObjISelDag(getAIObjTargetMachine()));
  return false;
}

FunctionPass *AIObjPassConfig::createTargetRegisterAllocator(bool) {
  return createAIObjRegisterAllocator();
}

// Modify the optimized compilation path to bypass optimized register alloction.
void AIObjPassConfig::addOptimizedRegAlloc(FunctionPass *RegAllocPass) {
  addFastRegAlloc(RegAllocPass);
}

bool AIObjPassConfig::addPostRegAlloc() {
  // PTXMFInfoExtract must after register allocation!
  //PM.add(createPTXMFInfoExtract(getPTXTargetMachine()));
  return false;
}

/// addPreEmitPass - This pass may be implemented by targets that want to run
/// passes immediately before machine code is emitted.  This should return
/// true if -print-machineinstrs should print out the code after the passes.
bool AIObjPassConfig::addPreEmitPass(){
  return false;
}
