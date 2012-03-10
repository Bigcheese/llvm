add_llvm_library(LLVMAIObjDesc
  AIObjMCAsmInfo.cpp
  AIObjMCTargetDesc.cpp
  )

add_dependencies(LLVMAIObjDesc AIObjCommonTableGen)
