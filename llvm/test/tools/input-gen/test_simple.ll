; ModuleID = 'test_simple.c'
source_filename = "test_simple.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@__inputgen_renamed_.str = internal unnamed_addr constant [4 x i8] c"%i\0A\00", align 1
@__record_function_pointers = local_unnamed_addr constant [2 x ptr] [ptr @__inputgen_renamed_add, ptr @__inputgen_renamed_main]
@__record_num_function_pointers = local_unnamed_addr constant i32 2

; Function Attrs: noinline nounwind memory(argmem: readwrite) uwtable
define dso_local void @__inputgen_renamed_add(ptr nocapture noundef readonly %0, ptr nocapture noundef readonly %1, ptr nocapture noundef writeonly %2, i32 noundef %3) local_unnamed_addr #0 {
  tail call void @__record_push() #5
  %5 = tail call ptr @__record_arg_ptr(ptr %0, i32 0) #5
  %6 = tail call ptr @__record_arg_ptr(ptr %1, i32 0) #5
  %7 = tail call ptr @__record_arg_ptr(ptr %2, i32 0) #5
  %8 = tail call i32 @__record_arg_i32(i32 %3) #5
  %9 = icmp sgt i32 %3, 0
  br i1 %9, label %10, label %12

10:                                               ; preds = %4
  %11 = zext nneg i32 %3 to i64
  br label %13

12:                                               ; preds = %13, %4
  tail call void @__record_pop() #5
  ret void

13:                                               ; preds = %10, %13
  %14 = phi i64 [ 0, %10 ], [ %22, %13 ]
  %15 = getelementptr inbounds i32, ptr %0, i64 %14
  tail call void @__record_access_i32(ptr %15, i64 0, i32 4, ptr %0, i32 0, ptr null, i32 0) #5
  %16 = load i32, ptr %15, align 4, !tbaa !5
  %17 = getelementptr inbounds i32, ptr %1, i64 %14
  tail call void @__record_access_i32(ptr %17, i64 0, i32 4, ptr %1, i32 0, ptr null, i32 0) #5
  %18 = load i32, ptr %17, align 4, !tbaa !5
  %19 = add nsw i32 %18, %16
  %20 = getelementptr inbounds i32, ptr %2, i64 %14
  %21 = zext i32 %19 to i64
  tail call void @__record_access_i32(ptr %20, i64 %21, i32 4, ptr %2, i32 1, ptr null, i32 0) #5
  store i32 %19, ptr %20, align 4, !tbaa !5
  %22 = add nuw nsw i64 %14, 1
  %23 = icmp eq i64 %22, %11
  br i1 %23, label %12, label %13, !llvm.loop !9
}

; Function Attrs: nounwind uwtable
define dso_local noundef i32 @__inputgen_renamed_main(i32 %0, ptr nocapture readnone %1) local_unnamed_addr #1 {
  %3 = tail call noalias dereferenceable_or_null(40) ptr @malloc(i64 noundef 40) #6
  %4 = tail call noalias dereferenceable_or_null(40) ptr @malloc(i64 noundef 40) #6
  %5 = tail call noalias dereferenceable_or_null(40) ptr @malloc(i64 noundef 40) #6
  %6 = tail call i64 @time(ptr noundef null) #5
  %7 = trunc i64 %6 to i32
  tail call void @srand(i32 noundef %7) #5
  br label %12

8:                                                ; preds = %12
  tail call void @__inputgen_renamed_add(ptr noundef nonnull %3, ptr noundef nonnull %4, ptr noundef %5, i32 noundef 10)
  %9 = getelementptr inbounds i8, ptr %5, i64 36
  tail call void @__record_access_i32(ptr nonnull %9, i64 0, i32 4, ptr %5, i32 0, ptr null, i32 0) #5
  %10 = load i32, ptr %9, align 4, !tbaa !5
  %11 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @__inputgen_renamed_.str, i32 noundef %10)
  ret i32 0

12:                                               ; preds = %2, %12
  %13 = phi i64 [ 0, %2 ], [ %21, %12 ]
  %14 = tail call i32 @rand() #5
  %15 = getelementptr inbounds i32, ptr %3, i64 %13
  %16 = zext i32 %14 to i64
  tail call void @__record_access_i32(ptr %15, i64 %16, i32 4, ptr %3, i32 1, ptr null, i32 0) #5
  store i32 %14, ptr %15, align 4, !tbaa !5
  %17 = tail call i32 @rand() #5
  %18 = sdiv i32 %17, 100
  %19 = getelementptr inbounds i32, ptr %4, i64 %13
  %20 = zext i32 %18 to i64
  tail call void @__record_access_i32(ptr %19, i64 %20, i32 4, ptr %4, i32 1, ptr null, i32 0) #5
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
declare i64 @time(ptr noundef) local_unnamed_addr #3

; Function Attrs: nounwind
declare i32 @rand() local_unnamed_addr #3

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr nocapture noundef readonly, ...) local_unnamed_addr #4

declare void @__record_access_i32(ptr, i64, i32, ptr, i32, ptr, i32) local_unnamed_addr

declare i32 @__record_arg_i32(ptr, i32) local_unnamed_addr

declare ptr @__record_arg_ptr(ptr, i32) local_unnamed_addr

declare void @__record_push() local_unnamed_addr

declare void @__record_pop() local_unnamed_addr

attributes #0 = { noinline nounwind memory(argmem: readwrite) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nounwind uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { mustprogress nofree nounwind willreturn allockind("alloc,uninitialized") allocsize(0) memory(inaccessiblemem: readwrite) "alloc-family"="malloc" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nofree nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #5 = { nounwind }
attributes #6 = { nounwind allocsize(0) }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"clang version 19.0.0git (git@github.com:maximilian-tech/llvm-project.git fb0436e185d615f913d2e0de576225933d4a5ee4)"}
!5 = !{!6, !6, i64 0}
!6 = !{!"int", !7, i64 0}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C/C++ TBAA"}
!9 = distinct !{!9, !10}
!10 = !{!"llvm.loop.mustprogress"}
!11 = distinct !{!11, !10}
