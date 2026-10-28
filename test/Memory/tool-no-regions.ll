; A barrier leaves nothing to report.
; RUN: %souper -souper-mem-opt -souper-mem-only %s | %FileCheck %s
declare void @ext()
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v) {
  store i32 %v, ptr %p, align 4
  call void @ext()
  %x = load i32, ptr %q, align 4
  ret i32 %x
}
; CHECK: ; Listing memory schedule replacements.
; CHECK-NOT: ; Function:
