set(LLVM_LINK_COMPONENTS support)

add_llvm_tool(llvm-cltest
  lld.h
  lld.cpp
  llvm-cltest.cpp
  Option.h
  Option.cpp
  )
