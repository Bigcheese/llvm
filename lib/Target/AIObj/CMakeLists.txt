set(LLVM_TARGET_DEFINITIONS AIObj.td)

tablegen(LLVM AIObjGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM AIObjGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM AIObjGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM AIObjGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM AIObjGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM AIObjGenCallingConv.inc -gen-callingconv)
add_public_tablegen_target(AIObjCommonTableGen)

add_llvm_target(AIObjCodeGen
  AIObjAsmPrinter.cpp
  AIObjInstrInfo.cpp
  AIObjISelDAGToDAG.cpp
  AIObjISelLowering.cpp
  AIObjFrameLowering.cpp
  AIObjMachineFunctionInfo.cpp
  AIObjMCAsmStreamer.cpp
  AIObjRegAlloc.cpp
  AIObjRegisterInfo.cpp
  AIObjSched.cpp
  AIObjSubtarget.cpp
  AIObjTargetMachine.cpp
  AIObjSelectionDAGInfo.cpp
  )

add_subdirectory(InstPrinter)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
