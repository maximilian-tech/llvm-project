; ModuleID = 'test_simple.c'
source_filename = "test_simple.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [4 x i8] c"%i\0A\00", align 1
@__record_function_pointers = local_unnamed_addr constant [0 x ptr] zeroinitializer
@__record_num_function_pointers = local_unnamed_addr constant i32 0

; Function Attrs: mustprogress noinline nounwind memory(argmem: readwrite) uwtable
define dso_local void @_Z3addPiS_S_i(ptr nocapture noundef readonly %0, ptr nocapture noundef readonly %1, ptr nocapture noundef writeonly %2, i32 noundef %3) local_unnamed_addr #0 {
  %5 = icmp sgt i32 %3, 0
  br i1 %5, label %6, label %8

6:                                                ; preds = %4
  %7 = zext nneg i32 %3 to i64
  br label %9

8:                                                ; preds = %9, %4
  ret void

9:                                                ; preds = %6, %9
  %10 = phi i64 [ 0, %6 ], [ %18, %9 ]
  %11 = getelementptr inbounds i32, ptr %0, i64 %10
  tail call void @__record_access_i32(ptr %11, i64 0, i32 4, ptr %0, i32 0, ptr null, i32 0) #5
  %12 = load i32, ptr %11, align 4, !tbaa !5
  %13 = getelementptr inbounds i32, ptr %1, i64 %10
  tail call void @__record_access_i32(ptr %13, i64 0, i32 4, ptr %1, i32 0, ptr null, i32 0) #5
  %14 = load i32, ptr %13, align 4, !tbaa !5
  %15 = add nsw i32 %14, %12
  %16 = getelementptr inbounds i32, ptr %2, i64 %10
  %17 = zext i32 %15 to i64
  tail call void @__record_access_i32(ptr %16, i64 %17, i32 4, ptr %2, i32 1, ptr null, i32 0) #5
  store i32 %15, ptr %16, align 4, !tbaa !5
  %18 = add nuw nsw i64 %10, 1
  %19 = icmp eq i64 %18, %7
  br i1 %19, label %8, label %9, !llvm.loop !9
}

; Function Attrs: mustprogress norecurse nounwind uwtable
define dso_local noundef i32 @main(i32 noundef %0, ptr nocapture noundef readnone %1) local_unnamed_addr #1 {
  tail call void @__record_push() #5
  %3 = tail call i32 @__record_arg_i32(i32 %0) #5
  %4 = tail call ptr @__record_arg_ptr(ptr %1, i32 0) #5
  %5 = tail call noalias dereferenceable_or_null(40) ptr @malloc(i64 noundef 40) #6
  %6 = tail call noalias dereferenceable_or_null(40) ptr @malloc(i64 noundef 40) #6
  %7 = tail call noalias dereferenceable_or_null(40) ptr @malloc(i64 noundef 40) #6
  tail call void @srand(i32 noundef 0) #5
  br label %12

8:                                                ; preds = %12
  tail call void @_Z3addPiS_S_i(ptr noundef nonnull %5, ptr noundef nonnull %6, ptr noundef %7, i32 noundef 10) #5
  %9 = getelementptr inbounds i8, ptr %7, i64 36
  tail call void @__record_access_i32(ptr nonnull %9, i64 0, i32 4, ptr %7, i32 0, ptr null, i32 0) #5
  %10 = load i32, ptr %9, align 4, !tbaa !5
  %11 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str, i32 noundef %10) #5
  tail call void @__record_pop() #5
  ret i32 0

12:                                               ; preds = %2, %12
  %13 = phi i64 [ 0, %2 ], [ %21, %12 ]
  %14 = tail call i32 @rand() #5
  %15 = getelementptr inbounds i32, ptr %5, i64 %13
  %16 = zext i32 %14 to i64
  tail call void @__record_access_i32(ptr %15, i64 %16, i32 4, ptr %5, i32 1, ptr null, i32 0) #5
  store i32 %14, ptr %15, align 4, !tbaa !5
  %17 = tail call i32 @rand() #5
  %18 = sdiv i32 %17, 100
  %19 = getelementptr inbounds i32, ptr %6, i64 %13
  %20 = zext i32 %18 to i64
  tail call void @__record_access_i32(ptr %19, i64 %20, i32 4, ptr %6, i32 1, ptr null, i32 0) #5
  store i32 %18, ptr %19, align 4, !tbaa !5
  %21 = add nuw nsw i64 %13, 1
  %22 = icmp eq i64 %21, 10
  br i1 %22, label %8, label %12, !llvm.loop !11
}

; Function Attrs: mustprogress nofree nounwind willreturn allockind("alloc,uninitialized") allocsize(0) memory(inaccessiblemem: readwrite)
declare noalias noundef ptr @malloc(i64 noundef) local_unnamed_addr #2

; Function Attrs: nounwind
declare void @srand(i32 noundef) local_unnamed_addr #3

; Function Attrs: nounwind
declare i32 @rand() local_unnamed_addr #3

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr nocapture noundef readonly, ...) local_unnamed_addr #4

declare void @__record_access_i32(ptr, i64, i32, ptr, i32, ptr, i32) local_unnamed_addr

declare i32 @__record_arg_i32(ptr, i32) local_unnamed_addr

declare ptr @__record_arg_ptr(ptr, i32) local_unnamed_addr

declare void @__record_push() local_unnamed_addr

declare void @__record_pop() local_unnamed_addr

attributes #0 = { mustprogress noinline nounwind memory(argmem: readwrite) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { mustprogress norecurse nounwind uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { mustprogress nofree nounwind willreturn allockind("alloc,uninitialized") allocsize(0) memory(inaccessiblemem: readwrite) "alloc-family"="malloc" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nofree nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #5 = { nounwind }
attributes #6 = { nounwind allocsize(0) }

!llvm.linker.options = !{}
!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"clang version 19.0.0git (git@github.com:maximilian-tech/llvm-project.git 2f2adfa3c330b4852de29cb75daa0cb77faa42ee)"}
!5 = !{!6, !6, i64 0}
!6 = !{!"int", !7, i64 0}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C++ TBAA"}
!9 = distinct !{!9, !10}
!10 = !{!"llvm.loop.mustprogress"}
!11 = distinct !{!11, !10}
