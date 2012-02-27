include_directories( ${CMAKE_CURRENT_BINARY_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/.. )

add_llvm_library(LLVMAIObjAsmPrinter
  AIObjInstPrinter.cpp
  )

add_dependencies(LLVMAIObjAsmPrinter AIObjCommonTableGen)
