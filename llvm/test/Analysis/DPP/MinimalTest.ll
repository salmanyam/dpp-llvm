; RUN: opt -S -passes="print<dpp-local>" -disable-output < %s 2>&1 | FileCheck %s --check-prefixes=CHECK,LOCAL
; RUN: opt -S -passes="print-dpp-global" -disable-output < %s 2>&1 | FileCheck %s --check-prefixes=CHECK,GLOBAL

; LOCAL: Data Pointer Prioritization Local Analysis
; GLOBAL: Data Pointer Prioritization Global Analysis
; CHECK-LABEL: hello_world
; CHECK-SAME: not implemented

@str = constant [12 x i8] c"Hello World\00", align 1

; Function Attrs: nofree nounwind uwtable
define void @hello_world() {
  %1 = tail call i32 @puts(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @str, i64 0, i64 0))
  ret void
}

declare i32 @puts(i8*)

