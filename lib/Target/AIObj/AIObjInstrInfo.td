//===-- AIObjInstrInfo.td - Target Description for AIObj Target -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the AIObj instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction format superclass
//===----------------------------------------------------------------------===//

include "AIObjInstrFormats.td"

def AIObjfunction_call    : SDNode<"AIOBJISD::FUNCTION_CALL", SDTNone,
                     [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;
def AIOBJexit_handler  : SDNode<"AIOBJISD::EXIT_HANDLER", SDTNone, [SDNPHasChain]>;

//===----------------------------------------------------------------------===//
// Instruction Pattern Stuff
//===----------------------------------------------------------------------===//

def PUSH_EVENT : InstAIObj<(outs RegI64:$d), (ins), "push_event", [(set RegI64:$d, (int_aiobj_push_event))]>;

def TEST : InstAIObj<(outs), (ins RegI64:$src), "TEST", [(int_aiobj_test RegI64:$src)]>;

let isReturn = 1, isTerminator = 1 in {
  def EXIT_HANDLER : InstAIObj<(outs), (ins), "exit_handler", [(AIOBJexit_handler)]>;
}

def FUNCTION_CALL : InstAIObj<(outs), (ins), "func_call", [(AIObjfunction_call)]>;

def FETCH_I : InstAIObj<(outs RegI64:$dst), (ins RegI64:$src), "fetch_i", [(set RegI64:$dst, (load RegI64:$src))]>;
def ASSIGN : InstAIObj<(outs), (ins RegI64:$val, RegI64:$addr), "assign", [(store RegI64:$val, RegI64:$addr)]>;

def PUSH_CONST : InstAIObj<(outs RegI64:$dst), (ins i64imm:$src), "push_const $src", [(set RegI64:$dst, imm:$src)]>;

def ADD : InstAIObj<(outs RegI64:$dst), (ins RegI64:$src1, RegI64:$src2), "add", [(set RegI64:$dst, (add RegI64:$src1, RegI64:$src2))]>;
