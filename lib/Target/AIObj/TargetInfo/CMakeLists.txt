include_directories( ${CMAKE_CURRENT_BINARY_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/.. )

add_llvm_library(LLVMAIObjInfo
  AIObjTargetInfo.cpp
  )

add_dependencies(LLVMAIObjInfo AIObjCommonTableGen)
