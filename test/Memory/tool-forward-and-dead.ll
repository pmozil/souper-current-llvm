; Forwarded loads and removed stores are named in the report.
; RUN: %souper -souper-mem-opt -souper-mem-only %s | %FileCheck %s
define i32 @fwd(ptr align 4 dereferenceable(4) %p, i32 %v) {
  store i32 %v, ptr %p, align 4
  %x = load i32, ptr %p, align 4
  ret i32 %x
}
define void @dead(ptr align 4 dereferenceable(4) %p, i32 %a, i32 %b) {
  store i32 %a, ptr %p, align 4
  store i32 %b, ptr %p, align 4
  ret void
}
; CHECK: ; Function: fwd
; CHECK: ; Load [1] takes the value stored by [0]
; CHECK: ; Function: dead
; CHECK: ; Store [0] is removed
