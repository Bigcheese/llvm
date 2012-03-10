; RUN: llc -march=aiobj < %s | FileCheck %s

%CreatureData = type <{ [8 x i8], double, double, double, i32, [60 x i8] ; 100
                      , i32, i32, i32, [18 x i8] ; 120
                      , i32, i32, i32, i32, i32, i32, i32, i32, i32, [4 x i8] ; 164
                      , i8*, [92 x i8] ; 264
                      , i32, [278 x i8] ; 456
                      , double, [8 x i8] ; 472
                      , double
                      ; Yeah, I'm way too lazy to manually do the rest of this, but you should get the point.
                      }>

%StaticObjectData = type opaque

%ItemData = type <{ [8 x i8], double, double, double, [4 x i8] ; 36
                  , i32, [64 x i8] ; 104
                  , i64, [4 x i8] ; 116
                  , i32, i32, [20 x i8] ; 144
                  , i32
                  }>

%GlobalObject = type <{ [52 x i8], i32 }>

%NpcEvent = type <{ [40 x i8], %CreatureData*, %CreatureData*, %CreatureData*, %CreatureData*, %CreatureData*, %CreatureData*, %CreatureData*, %CreatureData*, [8 x i8] ; 112, 10, 10
                  , %CreatureData*, %StaticObjectData*, %ItemData*, i8*, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, [4 x i8] ; 272, 35, 45
                  , i64, i64, i64, i64 ; 49
                  ; Not complete...
                  , %GlobalObject*
                  }>

%NpcMakerEvent = type <{ [40 x i8], %CreatureData*, %CreatureData*, %CreatureData*, %CreatureData*, %CreatureData*, %CreatureData*, %CreatureData*, %CreatureData*, [8 x i8] ; 112, 10, 10
                       , %CreatureData*, %StaticObjectData*, %ItemData*, i8*, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, [4 x i8] ; 272, 35, 45
                       , i64, i64, i64, i64 ; 49
                       ; Not complete...
                       , %GlobalObject*
                       }>

declare %NpcEvent* @llvm.aiobj.push.event() nounwind readnone
declare i64 @llvm.aiobj.test(i64) nounwind
declare i64 @llvm.aiobj.add.i64.i64(i64, i64) nounwind readnone
declare i8* @"\01GlobalObject.IntToStr"(%GlobalObject*, i64) nounwind readnone
declare void @"\01GlobalObject.Announce"(%GlobalObject*, i8*) nounwind

@"\01S0." = private unnamed_addr constant [6 x i8] c"Hello!", align 1

define void @TALKED() {
entry:
  %event = call %NpcEvent* @llvm.aiobj.push.event()
  %gg = getelementptr inbounds %NpcEvent* %event, i32 0, i32 48
  %ggval0 = load %GlobalObject** %gg
  call void @"\01GlobalObject.Announce"(%GlobalObject* %ggval0, i8* getelementptr inbounds ([6 x i8]* @"\01S0.", i64 0, i64 0))
  ret void
}

!0 = metadata !{ i32 1, metadata !"SizeofPointer", i32 8 }
!1 = metadata !{ i32 1, metadata !"SharedFactoryVersion", i32 69 }
!2 = metadata !{ i32 1, metadata !"NPCHVersion", i32 79 }
!3 = metadata !{ i32 1, metadata !"NASCVersion", i32 2 }
!4 = metadata !{ i32 1, metadata !"NPCEventHVersion", i32 2 }
!llvm.module.flags = !{ !0, !1, !2, !3, !4 }
