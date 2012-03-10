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

def brtarget : Operand<OtherVT>;

//===----------------------------------------------------------------------===//
// Instruction format superclass
//===----------------------------------------------------------------------===//

include "AIObjInstrFormats.td"

def AIObjfunction_call_void    : SDNode<"AIOBJISD::FUNCTION_CALL_VOID", SDTypeProfile<0, -1, []>,
                                        [SDNPHasChain, SDNPVariadic]>;
def AIObjfunction_call : SDNode<"AIOBJISD::FUNCTION_CALL", SDTypeProfile<1, -1, []>,
                                        [SDNPHasChain, SDNPVariadic]>;
def AIOBJexit_handler  : SDNode<"AIOBJISD::EXIT_HANDLER", SDTNone, [SDNPHasChain]>;

//===----------------------------------------------------------------------===//
// Instruction Pattern Stuff
//===----------------------------------------------------------------------===//

def PUSH_EVENT : InstAIObj<(outs RegI64:$d), (ins), "push_event", [(set RegI64:$d, (int_aiobj_push_event))]>;

def TEST : InstAIObj<(outs RegI64:$dst), (ins RegI64:$src), "TEST", [(set RegI64:$dst, (int_aiobj_test RegI64:$src))]>;

let isReturn = 1, isTerminator = 1 in {
  def EXIT_HANDLER : InstAIObj<(outs), (ins), "exit_handler", [(AIOBJexit_handler)]>;
}

let isBranch = 1, isTerminator = 1 in {
  def JUMP : InstAIObj<(outs), (ins brtarget:$d), "jump $d", [(br bb:$d)]>;
  def BRANCH_TRUE : InstAIObj<(outs), (ins brtarget:$dst, RegI64:$cond), "branch_true $dst", []>;
  def BRANCH_FALSE : InstAIObj<(outs), (ins brtarget:$dst), "branch_false $dst", []>;
}

def NOT_EQUAL : InstAIObj<(outs RegI64:$dst), (ins RegI64:$src1, RegI64:$src2), "not_equal", [(set RegI64:$dst, (setcc RegI64:$src1, RegI64:$src2, SETNE))]>;

def FUNCTION_CALL_VOID : InstAIObj<(outs), (ins), "func_call", [(AIObjfunction_call_void)]>;
def FUNCTION_CALL : InstAIObj<(outs RegI64:$dst), (ins), "func_call", [(set RegI64:$dst, (AIObjfunction_call))]>;

def FETCH_I : InstAIObj<(outs RegI64:$dst), (ins RegI64:$src), "fetch_i", [(set RegI64:$dst, (load RegI64:$src))]>;
def ASSIGN : InstAIObj<(outs), (ins RegI64:$val, RegI64:$addr), "assign", [(store RegI64:$val, RegI64:$addr)]>;

def PUSH_CONST : InstAIObj<(outs RegI64:$dst), (ins i64imm:$src), "push_const $src", [(set RegI64:$dst, imm:$src)]>;
def PUSH_STRING : InstAIObj<(outs RegI64:$dst), (ins RegI64:$src), "push_string $src", []>;

def ADD : InstAIObj<(outs RegI64:$dst), (ins RegI64:$src1, RegI64:$src2), "add", [(set RegI64:$dst, (add RegI64:$src1, RegI64:$src2))]>;

def PUSH_REG_SP : InstAIObj<(outs), (ins), "push_reg_sp", []>;
def STORE_TO_STACK_SLOT : InstAIObj<(outs), (ins i32imm:$src, i32imm:$uses), "STORE_TO_STACK_SLOT", []>;
def LOAD_FROM_STACK_SLOT : InstAIObj<(outs), (ins i32imm:$src), "LOAD_FROM_STACK_SLOT", []>;
def SHIFT_SP : InstAIObj<(outs), (ins i64imm:$src), "shift_sp $src", []>;
