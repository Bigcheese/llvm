set(LLVM_LINK_COMPONENTS support)

add_llvm_tool(llvm-cltest
  clang-driver.h
  clang-driver.cpp
  lld.h
  lld.cpp
  llvm-cltest.cpp
  Option.h
  Option.cpp
  )
