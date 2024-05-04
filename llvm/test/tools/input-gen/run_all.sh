#!/bin/bash
set -e

while read -r func_id func_name ; do
  gen="$1/input-gen.module.generate.a.out"
  "$gen" "$1" 0 2 "$func_name" "$func_id" 2> "$func_id.err.out" > "$func_id.log.out"

  run="$1/input-gen.module.run.a.out"
  for i in "$gen".input."$func_id".*.bin; do
    "$run" "$i" "$func_name" 2> "$i".err.out > "$i".log.out
  done;

done < "$1/available_functions"
