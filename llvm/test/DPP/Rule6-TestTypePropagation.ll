; RUN: opt -S -passes="print<dpp-local-myrule6>" -disable-output < %s 2>&1 | FileCheck %s --check-prefixes=CHECK,LOCAL
; XFAIL: *
; FIXME: Remove XFAIL when rule6 is implemented

; FIXME: This doesn't test anything useful, update when expectd output defined.
; CHECK-LABEL: I saw a function called bad!

; clang -Xclang -disable-lifetime-markers -fno-unroll-loops -O2
;
; typedef struct inner {
;     char *buffer1;
;     char *buffer2;
; } inner_t;
;
; typedef struct outer {
;     int number;
;     inner_t *ptr_to_inner;
;     inner_t nested_inner;
; } outer_t;
;
; void use_untyped(char *);
; void use_outer(outer_t *);
; void use_inner(inner_t *);
;
; int bad(void) {
;     outer_t A;
;
;     // Take ponter and cast to char * (okay)
;     char *untyped_mem = (char *) &A;
;
;     // Pass the char* (okay, but no idea what to expect)
;     use_untyped(untyped_mem);
;
;     // Pass into func by casting to outer_t (okay)
;     use_outer((outer_t *) untyped_mem);
;
;     // Pass into func by casting to inner_t (bad!)
;     use_inner((inner_t *) untyped_mem);
;
;     return 0;
; }
; ModuleID = 'Rule6-TestTypePropagation.c'
source_filename = "Rule6-TestTypePropagation.c"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.outer = type { i32, %struct.inner*, %struct.inner }
%struct.inner = type { i8*, i8* }

; Function Attrs: nounwind uwtable
define dso_local i32 @bad() local_unnamed_addr #0 {
  %1 = alloca %struct.outer, align 8
  %2 = bitcast %struct.outer* %1 to i8*
  call void @use_untyped(i8* nonnull %2) #2
  call void @use_outer(%struct.outer* nonnull %1) #2
  %3 = bitcast %struct.outer* %1 to %struct.inner*
  call void @use_inner(%struct.inner* nonnull %3) #2
  ret i32 0
}

declare dso_local void @use_untyped(i8*) local_unnamed_addr #1

declare dso_local void @use_outer(%struct.outer*) local_unnamed_addr #1

declare dso_local void @use_inner(%struct.inner*) local_unnamed_addr #1

attributes #0 = { nounwind uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nounwind }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 9.0.1 (git@github.com:ishkamiel/llvm.git c1a0a213378a458fbea1a5c77b315c7dce08fd05)"}
