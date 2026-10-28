; Byte-multiple widths that are not powers of two (i24) are handled.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i24 @f(ptr align 4 dereferenceable(4) %p, i24 %v) {
  store i24 %v, ptr %p, align 1
  %x = load i24, ptr %p, align 1
  ret i24 %x
}
; CHECK: store i24 %v
; CHECK-NOT: = load
; CHECK: ret i24 %v
