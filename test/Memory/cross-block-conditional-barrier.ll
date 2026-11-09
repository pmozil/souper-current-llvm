; A conditional branch is not crossed: the load is not executed on every path
; through the store's block, so it must not be hoisted above the store.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v, i1 %c) {
entry:
  store i32 %v, ptr %p, align 4
  br i1 %c, label %then, label %out
then:
  %x = load i32, ptr %q, align 4
  ret i32 %x
out:
  ret i32 0
}
; CHECK: store i32 %v, ptr %p
; CHECK: load i32, ptr %q
