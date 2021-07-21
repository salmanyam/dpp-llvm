; RUN: opt -S -passes="print-dpp-whitelist" -disable-output < %s 2>&1 | FileCheck %s --check-prefixes=CHECK

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@sink = global i8* null, align 8

declare void @llvm.memset.p0i8.i32(i8* %dest, i8 %val, i32 %len, i1 %isvolatile)
declare void @llvm.memcpy.p0i8.p0i8.i32(i8* %dest, i8* %src, i32 %len, i1 %isvolatile)
declare void @llvm.memmove.p0i8.p0i8.i32(i8* %dest, i8* %src, i32 %len, i1 %isvolatile)

; CHECK DPPWhilteList isSafe:
; CHECK-LABEL: @IB dso_preemptable{{$}}
; CHECK: %x
; CHECK-LABEL: @OOB dso_preemptable{{$}}
; CHECK-NOT: %x

define void @IB() {
entry:
  %x = alloca i32, i32 10, align 4
  %x1 = bitcast i32* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 36
  %x3 = bitcast i8* %x2 to i32*
  store i32 0, i32* %x3, align 1
  ret void
}

define void @OOB() {
entry:
  %x = alloca i32, i32 10, align 4
  %x1 = bitcast i32* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 40
  %x3 = bitcast i8* %x2 to i32*
  store i32 0, i32* %x3, align 1
  ret void
}
