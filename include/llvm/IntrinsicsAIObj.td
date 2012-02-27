//===- IntrinsicsAIObj.td - Defines AIObj intrinsics -------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the AIObj-specific intrinsics.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "aiobj" in {
  def int_aiobj_push_event : Intrinsic<[llvm_ptr_ty], [], [IntrNoMem]>;
  def int_aiobj_test : Intrinsic<[], [llvm_i64_ty], []>;

  // Arithmetic intrins llvm_i64_ty llvm_double_ty
  def int_aiobj_add_i64_i64 : Intrinsic<
    [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty], [IntrNoMem, Commutative]>;
  def int_aiobj_add_i64_f64 : Intrinsic<
    [llvm_double_ty], [llvm_i64_ty, llvm_double_ty], [IntrNoMem, Commutative]>;
  def int_aiobj_add_f64_i64 : Intrinsic<
    [llvm_double_ty], [llvm_double_ty, llvm_i64_ty], [IntrNoMem, Commutative]>;
  def int_aiobj_add_f64_f64 : Intrinsic<
    [llvm_double_ty], [llvm_double_ty, llvm_double_ty], [IntrNoMem, Commutative]>;
}
