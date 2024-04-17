#!/usr/bin/env bash

set -e
set -x


NODES=3
TIMEOUT=1h

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
CURDATE=$(date +"%Y-%m-%dT%H:%M:%S%z")
JOB_LOG_DIR="$LCWS/results/jobs/$CURDATE/"
mkdir -p "$JOB_LOG_DIR"
JOB_NAME="compile-inputgen"
JOB_LOG="$JOB_LOG_DIR/job-$JOB_NAME.main.out"
JOB_LOG_CHILD="$JOB_LOG_DIR/job-$JOB_NAME.child.out"

flux submit -N 1 -x -t "$TIMEOUT" --output="$JOB_LOG" "$SCRIPT_DIR/flux_job.sh" "$NODES"  "$TIMEOUT" "$JOB_LOG_CHILD"
