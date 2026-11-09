; Disjoint accesses still reorder under a big-endian data layout.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
target datalayout = "E-m:e-p:64:64-i64:64-n32:64"
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v) {
  store i32 %v, ptr %p, align 4
  %x = load i32, ptr %q, align 4
  %y = add i32 %x, 1
  ret i32 %y
}
; CHECK: load i32
; CHECK-NEXT: store i32
