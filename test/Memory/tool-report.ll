; The souper tool lists memory replacements without touching the IR: which
; accesses changed, the new order, and the replacement in Souper's format.
; RUN: %souper -souper-mem-opt -souper-mem-only %s | %FileCheck %s
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v) {
  store i32 %v, ptr %p, align 4
  %x = load i32, ptr %q, align 4
  ret i32 %x
}
; CHECK: ; Function: f
; CHECK-NEXT: ; Memory region: 2 accesses, cost 9 -> 8
; CHECK-NEXT: ;   [0] store i32 %v, ptr %p, align 4
; CHECK-NEXT: ;   [1] %x = load i32, ptr %q, align 4
; CHECK-NEXT: ; New order: 1 0
; CHECK: = memresult
; CHECK: cand
