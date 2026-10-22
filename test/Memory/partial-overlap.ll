; An i8 load inside a stored i32 must not be forwarded or moved above the store.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i8 @f(ptr align 4 dereferenceable(4) %p, i32 %v) {
  store i32 %v, ptr %p, align 4
  %p1 = getelementptr inbounds i8, ptr %p, i64 1
  %x = load i8, ptr %p1, align 1
  ret i8 %x
}
; CHECK: store i32 %v, ptr %p
; CHECK: load i8
