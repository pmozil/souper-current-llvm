; -souper-mem-emit-ir applies the replacements and writes the rewritten IR.
; RUN: %souper -souper-mem-opt -souper-mem-only -souper-mem-emit-ir=%t.ll %s > /dev/null
; RUN: %FileCheck %s --input-file=%t.ll
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v) {
  store i32 %v, ptr %p, align 4
  %x = load i32, ptr %q, align 4
  ret i32 %x
}
; CHECK: define i32 @f
; CHECK-NEXT: %x = load i32, ptr %q
; CHECK-NEXT: store i32 %v, ptr %p
