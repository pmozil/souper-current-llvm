; A forwarded load is itself stored and loaded back. Both loads must resolve to
; the original stored value; replacing them in the wrong order used to leave a
; use of an erased load.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i32 @f(ptr align 4 dereferenceable(4) %p, ptr align 4 dereferenceable(4) %q, i32 %v) {
  store i32 %v, ptr %p, align 4
  %a = load i32, ptr %p, align 4
  store i32 %a, ptr %q, align 4
  %b = load i32, ptr %q, align 4
  %r = add i32 %a, %b
  ret i32 %r
}
; CHECK-NOT: = load
; CHECK: store i32 %v, ptr %p
; CHECK-NEXT: store i32 %v, ptr %q
