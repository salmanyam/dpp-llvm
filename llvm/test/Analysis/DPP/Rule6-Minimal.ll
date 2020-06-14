; RUN: opt -S -passes="print<dpp-local-rule6>" -disable-output < %s 2>&1 | FileCheck %s --check-prefixes=CHECK,LOCAL
; RUN: opt -S -passes="print-dpp-global-rule6" -disable-output < %s 2>&1 | FileCheck %s --check-prefixes=CHECK,GLOBAL

; LOCAL: DPPRule6L
; GLOBAL: DPPRule6G
; GLOBAL-LABEL: {{^Globals:$}}
; GLOBAL-LABEL: {{^Functions:$}}
; LOCAL-LABEL: main
; LOCAL: alloca %struct.mystruct_s

; clang -Xclang -disable-lifetime-markers -fno-unroll-loops -O2 -emit-llvm -c -S

; #include <stdio.h>
;
; typedef struct mystruct_s {
;     char buffer[64];
;     int (*fp)(const char*, ...);
; } mystruct_t;
;
; void func(mystruct_t *ms) {
;     gets(ms->buffer);
;     ms->fp(ms->buffer);
; }
;
; int main(void) {
;     mystruct_t ms = {"Hello World\n", &printf};
;     func(&ms);
;     return 0;
; }
; ModuleID = 'Rule6-Minimal.c'
source_filename = "Rule6-Minimal.c"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.mystruct_s = type { [64 x i8], i32 (i8*, ...)* }

@__const.main.ms = private unnamed_addr constant %struct.mystruct_s { [64 x i8] c"Hello World\0A\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00", i32 (i8*, ...)* @printf }, align 8

; Function Attrs: nounwind uwtable
define dso_local void @func(%struct.mystruct_s*) local_unnamed_addr #0 {
  %2 = getelementptr inbounds %struct.mystruct_s, %struct.mystruct_s* %0, i64 0, i32 0, i64 0
  %3 = tail call i32 (i8*, ...) bitcast (i32 (...)* @gets to i32 (i8*, ...)*)(i8* %2) #4
  %4 = getelementptr inbounds %struct.mystruct_s, %struct.mystruct_s* %0, i64 0, i32 1
  %5 = load i32 (i8*, ...)*, i32 (i8*, ...)** %4, align 8, !tbaa !2
  %6 = tail call i32 (i8*, ...) %5(i8* %2) #4
  ret void
}

declare dso_local i32 @gets(...) local_unnamed_addr #1

; Function Attrs: nounwind uwtable
define dso_local i32 @main() local_unnamed_addr #0 {
  %1 = alloca %struct.mystruct_s, align 8
  %2 = getelementptr inbounds %struct.mystruct_s, %struct.mystruct_s* %1, i64 0, i32 0, i64 0
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* nonnull align 8 %2, i8* align 8 getelementptr inbounds (%struct.mystruct_s, %struct.mystruct_s* @__const.main.ms, i64 0, i32 0, i64 0), i64 72, i1 false)
  %3 = call i32 (i8*, ...) bitcast (i32 (...)* @gets to i32 (i8*, ...)*)(i8* nonnull %2) #4
  %4 = getelementptr inbounds %struct.mystruct_s, %struct.mystruct_s* %1, i64 0, i32 1
  %5 = load i32 (i8*, ...)*, i32 (i8*, ...)** %4, align 8, !tbaa !2
  %6 = call i32 (i8*, ...) %5(i8* nonnull %2) #4
  ret i32 0
}

; Function Attrs: nofree nounwind
declare dso_local i32 @printf(i8* nocapture readonly, ...) #2

; Function Attrs: argmemonly nounwind
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* nocapture writeonly, i8* nocapture readonly, i64, i1 immarg) #3

attributes #0 = { nounwind uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nofree nounwind "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { argmemonly nounwind }
attributes #4 = { nounwind }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 9.0.1 (git@github.com:ishkamiel/llvm.git c1a0a213378a458fbea1a5c77b315c7dce08fd05)"}
!2 = !{!3, !6, i64 64}
!3 = !{!"mystruct_s", !4, i64 0, !6, i64 64}
!4 = !{!"omnipotent char", !5, i64 0}
!5 = !{!"Simple C/C++ TBAA"}
!6 = !{!"any pointer", !4, i64 0}