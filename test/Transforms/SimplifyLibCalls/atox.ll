; RUN: opt < %s -simplify-libcalls -S | FileCheck %s

target datalayout = "e-p:32:32"
@.str = private constant [4 x i8] c"NaN\00"
@.str1 = private constant [2 x i8] c"2\00"
@.str2 = private constant [2 x i8] c"3\00"
@.str3 = private constant [2 x i8] c"4\00"

declare double @atof(i8*)
declare i32 @atoi(i8*)
declare i32 @atol(i8*)
declare i64 @atoll(i8*)

define i32 @main() {
entry:
  %call.0 = tail call double @atof(i8* getelementptr inbounds ([4 x i8]* @.str, i32 0, i32 0))
  %call.1 = tail call i32 @atoi(i8* getelementptr inbounds ([2 x i8]* @.str1, i32 0, i32 0))
  %call.2 = tail call i32 @atol(i8* getelementptr inbounds ([2 x i8]* @.str2, i32 0, i32 0))
  %call.3 = tail call i64 @atoll(i8* getelementptr inbounds ([2 x i8]* @.str3, i32 0, i32 0))
  %conv = fptosi double %call.0 to i32
  %conv8 = trunc i64 %call.3 to i32
  %add = add nsw i32 %conv, %call.1
  %add6 = add nsw i32 %add, %call.2
  %add9 = add nsw i32 %add6, %conv8
  ret i32 %add9
}
