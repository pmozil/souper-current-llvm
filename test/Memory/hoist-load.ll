; AA proves the two noalias arguments disjoint, so the load moves above the store.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v) {
  store i32 %v, ptr %p, align 4
  %x = load i32, ptr %q, align 4
  %y = add i32 %x, 1
  ret i32 %y
}
; CHECK: load i32
; CHECK-NEXT: store i32
