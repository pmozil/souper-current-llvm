; The loop header has two predecessors, so its edge is never crossed.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v, i32 %n) {
entry:
  store i32 %v, ptr %p, align 4
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %x = load i32, ptr %q, align 4
  %i2 = add i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %x
}
; CHECK: store i32 %v, ptr %p
; CHECK: loop:
; CHECK: load i32, ptr %q
