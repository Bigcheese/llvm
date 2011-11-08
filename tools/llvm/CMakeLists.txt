##===- tools/llvm/CMakeLists.txt ------------------------------------------===##
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
##===----------------------------------------------------------------------===##

#add_subdirectory(bugpoint)
#add_subdirectory(diff)

set(LLVM_LINK_COMPONENTS
  Support
  )

add_llvm_tool(llvm
  llvm.cpp
  ar.cpp
  as.cpp
  bcanalyzer.cpp
  bugpoint.cpp
  c.cpp
  cov.cpp
  diff.cpp
  dis.cpp
  extract.cpp
  i.cpp
  ld.cpp
  link.cpp
  mc.cpp
  opt.cpp
  prof.cpp
  ranlib.cpp
  )
