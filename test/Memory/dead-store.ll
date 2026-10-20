; The first store is overwritten before anything reads it.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define void @f(ptr align 4 dereferenceable(4) %p, i32 %a, i32 %b) {
  store i32 %a, ptr %p, align 4
  store i32 %b, ptr %p, align 4
  ret void
}
; CHECK: store i32 %b
; CHECK-NOT: store i32 %a
