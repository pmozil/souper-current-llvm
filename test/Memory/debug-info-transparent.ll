; Debug records neither split a region nor are moved by it, so the load is still
; hoisted above the store in a -g build.
; RUN: %opt -load-pass-plugin %pass -passes='function(souper),dce' -souper-mem-opt -S -o - %s | %FileCheck %s
define i32 @f(ptr noalias align 4 dereferenceable(4) %p, ptr noalias align 4 dereferenceable(4) %q, i32 %v) !dbg !6 {
  store i32 %v, ptr %p, align 4
  #dbg_value(i32 %v, !9, !DIExpression(), !10)
  %x = load i32, ptr %q, align 4
  %y = add i32 %x, 1
  ret i32 %y
}
; CHECK: load i32, ptr %q
; CHECK: store i32 %v, ptr %p

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3}
!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "t", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "t.c", directory: "/")
!3 = !{i32 2, !"Debug Info Version", i32 3}
!6 = distinct !DISubprogram(name: "f", scope: !1, file: !1, line: 1, type: !7, scopeLine: 1, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !{})
!7 = !DISubroutineType(types: !{null})
!9 = !DILocalVariable(name: "v", scope: !6, file: !1, line: 1, type: !11)
!10 = !DILocation(line: 1, column: 1, scope: !6)
!11 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
