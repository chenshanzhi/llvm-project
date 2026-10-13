; RUN: llc -mtriple=aarch64-unknown-linux-gnu -mcpu=hip12 -verify-machineinstrs < %s | FileCheck %s --check-prefix=HBC
; RUN: llc -mtriple=aarch64-unknown-linux-gnu -mcpu=hip12 -global-isel < %s | FileCheck %s --check-prefix=HBC
; RUN: llc -mtriple=aarch64-unknown-linux-gnu -mcpu=hip12 -O0 < %s | FileCheck %s --check-prefix=HBC
; Enabled explicitly on another CPU.
; RUN: llc -mtriple=aarch64-unknown-linux-gnu -mattr=+hbc,+stack-guard-hbc < %s | FileCheck %s --check-prefix=HBC
; Disabled explicitly on hip12.
; RUN: llc -mtriple=aarch64-unknown-linux-gnu -mcpu=hip12 -mattr=-stack-guard-hbc < %s | FileCheck %s --check-prefix=NOHBC
; Disabled on hip12 by turning FEAT_HBC itself off.
; RUN: llc -mtriple=aarch64-unknown-linux-gnu -mcpu=hip12 -mattr=-hbc < %s | FileCheck %s --check-prefix=NOHBC
; Off by default: the hbc feature alone is not enough.
; RUN: llc -mtriple=aarch64-unknown-linux-gnu -mattr=+hbc < %s | FileCheck %s --check-prefix=NOHBC
; The policy feature alone is not enough without FEAT_HBC.
; RUN: llc -mtriple=aarch64-unknown-linux-gnu -mattr=+stack-guard-hbc < %s | FileCheck %s --check-prefix=NOHBC

; FEAT_HBC adds BC.cond, a variant of B.cond hinting that the branch is
; nearly always resolved the same way. The AArch64StackGuardHBC pass rewrites
; the (almost never taken) stack protector check branch to use it. The
; rewrite is gated on the "stack-guard-hbc" subtarget feature, which is off
; by default and enabled by -mcpu=hip12.

define i32 @stack_guard_check(ptr nocapture readonly %src) #0 {
; HBC-LABEL: stack_guard_check:
; HBC: adrp
; HBC: ldr
; HBC: str
; HBC: bc.ne
; HBC: bl __stack_chk_fail

; NOHBC-LABEL: stack_guard_check:
; NOHBC: adrp
; NOHBC: ldr
; NOHBC: str
; NOHBC: b.ne
; NOHBC: bl __stack_chk_fail
entry:
  %buf = alloca [32 x i8], align 1
  call void @llvm.memcpy.p0.p0.i64(ptr %buf, ptr %src, i64 32, i1 false)
  %len = call i64 @strlen(ptr %buf)
  %conv = trunc i64 %len to i32
  ret i32 %conv
}

; A function without a stack protector must not contain any BC.cond.

define i32 @no_stack_guard(i32 %x) {
; HBC-LABEL: no_stack_guard:
; HBC-NOT: bc.
entry:
  %add = add nsw i32 %x, 1
  ret i32 %add
}

declare i64 @strlen(ptr nocapture)
declare void @llvm.memcpy.p0.p0.i64(ptr nocapture writeonly, ptr nocapture readonly, i64, i1)

attributes #0 = { sspstrong }
