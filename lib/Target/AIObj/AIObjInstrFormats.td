//===-- AIObjInstrFormats.td - AIObj Instruction Formats ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Instruction Types
let Namespace = "AIObj" in {
  class InstAIObj<dag oops, dag iops, string asmstr, list<dag> pattern>
    : Instruction {
      dag OutOperandList = oops;
      dag InOperandList = iops;
      let AsmString = asmstr;
      let Pattern = pattern;
      let isPredicable = 1;
  }
}
