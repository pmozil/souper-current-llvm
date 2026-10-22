; A convergent call that touches no memory is still ordering sensitive, so it
; ends the region and nothing moves around it.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
declare i32 @conv(i32) convergent nounwind willreturn memory(none)
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v) {
  store i32 %v, ptr %p, align 4
  %c = call i32 @conv(i32 %v)
  %x = load i32, ptr %q, align 4
  %y = add i32 %x, %c
  ret i32 %y
}
; CHECK: store i32 %v, ptr %p
; CHECK-NEXT: call i32 @conv
; CHECK-NEXT: load i32, ptr %q
