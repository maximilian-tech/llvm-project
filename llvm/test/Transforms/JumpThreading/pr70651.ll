; NOTE: Assertions have been autogenerated by utils/update_test_checks.py UTC_ARGS: --version 3
; RUN: opt -S -passes=jump-threading < %s | FileCheck %s

; FIXME: This is a miscompile.
define i64 @test(i64 %v) {
; CHECK-LABEL: define i64 @test(
; CHECK-SAME: i64 [[V:%.*]]) {
; CHECK-NEXT:  entry:
; CHECK-NEXT:    [[V_NONNEG:%.*]] = icmp sgt i64 [[V]], -1
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[SUM:%.*]] = phi i64 [ 0, [[ENTRY:%.*]] ], [ [[SUM_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[SUM_NEXT]] = add i64 [[SUM]], [[V]]
; CHECK-NEXT:    [[OVERFLOW:%.*]] = icmp ult i64 [[SUM_NEXT]], [[SUM]]
; CHECK-NEXT:    br i1 [[V_NONNEG]], label [[FOR_BODY]], label [[EXIT:%.*]]
; CHECK:       exit:
; CHECK-NEXT:    ret i64 [[SUM]]
;
entry:
  %v.nonneg = icmp sgt i64 %v, -1
  br label %for.body

for.body:
  %sum = phi i64 [ 0, %entry ], [ %sum.next, %for.body ]
  %sum.next = add i64 %sum, %v
  %overflow = icmp ult i64 %sum.next, %sum
  %cmp = xor i1 %v.nonneg, %overflow
  br i1 %cmp, label %for.body, label %exit

exit:
  ret i64 %sum
}
