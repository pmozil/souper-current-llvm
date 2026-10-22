; llvm.sideeffect has ordering semantics (it reads and writes inaccessible
; memory), so it ends the region.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
declare void @llvm.sideeffect() inaccessiblememonly nounwind willreturn
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v) {
  store i32 %v, ptr %p, align 4
  call void @llvm.sideeffect()
  %x = load i32, ptr %q, align 4
  ret i32 %x
}
; CHECK: store i32 %v, ptr %p
; CHECK-NEXT: call void @llvm.sideeffect
; CHECK-NEXT: load i32, ptr %q
