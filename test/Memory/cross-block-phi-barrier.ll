; The join block has a PHI and two predecessors: a hard barrier.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v, i1 %c) {
entry:
  store i32 %v, ptr %p, align 4
  br i1 %c, label %a, label %b
a:
  br label %join
b:
  br label %join
join:
  %m = phi i32 [ 1, %a ], [ 2, %b ]
  %x = load i32, ptr %q, align 4
  %r = add i32 %x, %m
  ret i32 %r
}
; CHECK: store i32 %v, ptr %p
; CHECK: load i32, ptr %q
