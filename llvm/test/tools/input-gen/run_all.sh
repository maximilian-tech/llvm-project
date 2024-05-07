#!/bin/bash
set -e

while read -r func_id func_name ; do
  gen="$1/input-gen.module.generate.a.out"
  "$gen" "$1" 0 2 "$func_name" "$func_id" 2> "$1/$func_id.gen.err.out" > "$1/$func_id.gen.log.out"

  run="$1/input-gen.module.run.a.out"
  for i in "$gen".input."$func_id".*.bin; do
    "$run" "$i" "$func_name" 2> "$i".run.err.out > "$i".run.log.out
  done;

done < "$1/available_functions"
