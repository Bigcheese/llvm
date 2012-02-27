//===-- AIObj.td - Describe the AIObj Target Machine -------*- tablegen -*-===//
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

//===----------------------------------------------------------------------===//
// Target-independent interfaces which we are implementing
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Register File, Calling Conv, Instruction Descriptions
//===----------------------------------------------------------------------===//

include "AIObjRegisterInfo.td"
include "AIObjCallingConv.td"
include "AIObjInstrInfo.td"

def AIObjInstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// AIOBJ processors supported.
//===----------------------------------------------------------------------===//

class Proc<string Name, list<SubtargetFeature> Features>
 : Processor<Name, NoItineraries, Features>;

def : Proc<"god", []>;

//===----------------------------------------------------------------------===//
// Assembly printer
//===----------------------------------------------------------------------===//
// AIObj uses the MC printer for asm output, so make sure the TableGen
// AsmWriter bits get associated with the correct class.
def AIObjAsmWriter : AsmWriter {
  string AsmWriterClassName  = "InstPrinter";
  bit isMCAsmWriter = 1;
}

//===----------------------------------------------------------------------===//
// Declare the target which we are implementing
//===----------------------------------------------------------------------===//

def AIObj : Target {
  let InstructionSet = AIObjInstrInfo;
  let AssemblyWriters = [AIObjAsmWriter];
}
