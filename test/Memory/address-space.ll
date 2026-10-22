; Accesses through a non-default address space are not modelled: the extractor
; treats them as barriers, so nothing is reordered.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i32 @f(ptr addrspace(1) noalias align 4 dereferenceable(4) %p, ptr addrspace(1) noalias align 4 dereferenceable(4) %q, i32 %v) {
  store i32 %v, ptr addrspace(1) %p, align 4
  %x = load i32, ptr addrspace(1) %q, align 4
  ret i32 %x
}
; CHECK: store i32 %v, ptr addrspace(1) %p
; CHECK-NEXT: load i32, ptr addrspace(1) %q
