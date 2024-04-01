#!/bin/bash
set -e

for f in ${1}/*.generate.a.out; do
  $f ${1} 0 2 2> ${f}.err.out > ${f}.log.out

  e="${f%.generate.a.out}.run.a.out"
  for i in $f.input.*.bin; do
    $e ${i} 2> ${i}.err.out > ${i}.log.out
  done;

done;
