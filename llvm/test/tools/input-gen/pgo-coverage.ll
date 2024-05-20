; RUN: mkdir -p /tmp/test1
; RUN: input-gen --verify --output-dir /tmp/test1 --compile-input-gen-executables --input-gen-runtime %S/../../../../input-gen-runtimes/rt-input-gen.cpp --input-run-runtime %S/../../../../input-gen-runtimes/rt-run.cpp --instrumented-module-for-coverage --profiling-runtime-path=/tmp/llvm-project/build/lib/clang/19/lib/x86_64-unknown-linux-gnu/libclang_rt.profile.a %s
; RUN: %S/run_all.sh /tmp/test1
; RUN: llvm-profdata merge /tmp/test1/i64.profraw /tmp/test1/0.profraw -o /tmp/test1/coverage.prof

define dso_local i64 @i64(ptr noundef %LL) local_unnamed_addr #0 {
entry:
  %a = load i64, ptr %LL
  store i64 %a, ptr %LL
  ret i64 %a
}
