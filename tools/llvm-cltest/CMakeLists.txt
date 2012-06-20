set(LLVM_LINK_COMPONENTS support)

add_llvm_tool(llvm-cltest
  clang-driver.h
  clang-driver.cpp
  link-options.h
  link-options.cpp
  lld.h
  lld.cpp
  lld-core-options.h
  lld-core-options.cpp
  llvm-cltest.cpp
  Option.h
  Option.cpp
  )
