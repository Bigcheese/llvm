##===- unittests/ADT/Makefile ------------------------------*- Makefile -*-===##
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
##===----------------------------------------------------------------------===##

LEVEL = ../..
TESTNAME = Option
LINK_COMPONENTS := support
BUILT_SOURCES = Opts.inc

include $(LEVEL)/Makefile.config
include $(LLVM_SRC_ROOT)/unittests/Makefile.unittest

$(ObjDir)/Opts.inc : Opts.td $(LLVM_TBLGEN) $(ObjDir)/.dir
	$(Echo) "Building Clang Driver Option tables with tblgen"
	$(Verb) $(LLVMTableGen) -gen-opt-parser-defs -o $(call SYSPATH, $@) $<
