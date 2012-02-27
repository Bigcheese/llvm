add_llvm_library(LLVMAIObjDesc
  AIObjMCTargetDesc.cpp
  AIObjMCAsmInfo.cpp
  )

add_dependencies(LLVMAIObjDesc AIObjCommonTableGen)
