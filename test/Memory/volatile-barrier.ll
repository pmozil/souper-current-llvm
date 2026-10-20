; A volatile access ends a region, so nothing around it is reordered.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v) {
  store volatile i32 %v, ptr %p, align 4
  %x = load i32, ptr %q, align 4
  ret i32 %x
}
; CHECK: store volatile
; CHECK-NEXT: load i32
