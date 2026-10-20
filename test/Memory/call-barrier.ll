; A call ends a region.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
declare void @ext()
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v) {
  store i32 %v, ptr %p, align 4
  call void @ext()
  %x = load i32, ptr %q, align 4
  ret i32 %x
}
; CHECK: store i32
; CHECK-NEXT: call void @ext
; CHECK-NEXT: load i32
