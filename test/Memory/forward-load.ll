; The load takes the value just stored.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i32 @f(ptr align 4 dereferenceable(4) %p, i32 %v) {
  store i32 %v, ptr %p, align 4
  %x = load i32, ptr %p, align 4
  %y = mul i32 %x, 3
  ret i32 %y
}
; CHECK-NOT: = load
; CHECK: mul i32 %v, 3
