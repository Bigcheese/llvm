# RUN: llvm-mc -triple i386-unknown-unknown %s -filetype obj -o - \
# RUN:   | llvm-readobj -t | FileCheck %s

	.end

its_a_tarp:
	int $0x3

# CHECK: Symbols [
# CHECK-NOT:   Name: its_a_tarp

