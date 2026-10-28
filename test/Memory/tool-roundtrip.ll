; What the tool prints is a replacement souper-check can verify on its own.
; RUN: %souper -souper-mem-opt -souper-mem-only %s > %t
; RUN: %souper-check %t | %FileCheck %s
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v) {
  store i32 %v, ptr %p, align 4
  %x = load i32, ptr %q, align 4
  ret i32 %x
}
; CHECK: ; LGTM
