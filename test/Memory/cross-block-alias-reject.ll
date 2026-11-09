; The pointers may alias, so the load must stay after the store.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i32 @f(ptr align 4 dereferenceable(4) %p, ptr align 4 dereferenceable(4) %q, i32 %v) {
entry:
  store i32 %v, ptr %p, align 4
  br label %next
next:
  %x = load i32, ptr %q, align 4
  ret i32 %x
}
; CHECK: store i32 %v, ptr %p
; CHECK: load i32, ptr %q
