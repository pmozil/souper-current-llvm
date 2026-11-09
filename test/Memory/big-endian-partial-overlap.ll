; Byte 1 of a stored i32 is a different byte per endianness; the narrow load
; must stay after the store and must not be forwarded under either layout.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
target datalayout = "E-m:e-p:64:64-i64:64-n32:64"
define i8 @f(ptr align 4 dereferenceable(4) %p, i32 %v) {
  store i32 %v, ptr %p, align 4
  %p1 = getelementptr inbounds i8, ptr %p, i64 1
  %x = load i8, ptr %p1, align 1
  ret i8 %x
}
; CHECK: store i32 %v, ptr %p
; CHECK: load i8
