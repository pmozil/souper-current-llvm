; Store-to-load forwarding across an unconditional edge.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i32 @f(ptr align 4 dereferenceable(4) %p, i32 %v) {
entry:
  store i32 %v, ptr %p, align 4
  br label %next
next:
  %x = load i32, ptr %p, align 4
  %y = add i32 %x, 1
  ret i32 %y
}
; CHECK-NOT: load i32
; CHECK: add i32 %v, 1
