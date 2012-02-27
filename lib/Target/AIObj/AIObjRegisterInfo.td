//===-- AIObjRegisterInfo.td - AIObj Register defs ---------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the AIObj register file
//===----------------------------------------------------------------------===//

class AIObjReg<string n> : Register<n> {
  let Namespace = "AIObj";
}

//===----------------------------------------------------------------------===//
//  Registers
//===----------------------------------------------------------------------===//

// The generated register info code throws warnings for empty register classes
// (e.g. zero-length arrays), so we use a dummy register here just to prevent
// these warnings.
def DUMMY_REG : AIObjReg<"R0">;

//===----------------------------------------------------------------------===//
//  Register classes
//===----------------------------------------------------------------------===//
def RegI64 : RegisterClass<"AIObj", [i64], 64, (add DUMMY_REG)>;
