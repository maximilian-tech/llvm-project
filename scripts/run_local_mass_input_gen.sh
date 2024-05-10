#!/usr/bin/env bash

set -e

module load python/3.10.8

if [ "$SINGLE" != "" ]; then
    START="$SINGLE"
    END="$(("$SINGLE" + 1))"
fi

START=${START:=0}
END=${END:=7000}
NUM_CPU=${NUM_CPU:="$(nproc --all)"}

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
CURDATE=$(date +"%Y-%m-%dT%H:%M:%S%z")
JOB_LOG_DIR="$LCWS/results/jobs/$CURDATE/"
mkdir -p "$JOB_LOG_DIR"
JOB_NAME="compile-inputgen"
JOB_LOG="$JOB_LOG_DIR/job-$JOB_NAME.main.out"

SCRIPT="$SCRIPT_DIR/mass_input_gen.py"

if [ "$JUG" == "run" ]; then
    JUG_RUN="jug-execute --will-cite"
    SCRIPT="$SCRIPT_DIR/jugfile.py"
    DASHDASH=--
elif [ "$JUG" == "status" ]; then
    JUG_RUN="jug status"
    SCRIPT="$SCRIPT_DIR/jugfile.py"
    ADDITIONAL_FLAGS="$ADDITIONAL_FLAGS --get-jug-results"
    DASHDASH=--
elif [ "$JUG" == "results" ]; then
    JUG_RUN=
    SCRIPT="$SCRIPT_DIR/print_mig_jug_results.py"
    ADDITIONAL_FLAGS="$ADDITIONAL_FLAGS --get-jug-results"
elif [ "$JUG" != "" ]; then
    echo Invalid JUG option 1>&2
    exit 1
fi

if [ "$NOCLEANUP" == "" ]; then
    ADDITIONAL_FLAGS="$ADDITIONAL_FLAGS --cleanup"
fi

if [ "$JUG" == "" ]; then
    echo "$JOB_LOG"
    echo tail -f "$JOB_LOG"
    echo vim "$JOB_LOG"
    echo less "$JOB_LOG"
fi

. "$SCRIPT_DIR/enable.sh" "/usr/WS1/$USER/opt/input-gen-release"
export PYTHONPATH="$PYTHONPATH:$SCRIPT_DIR"

function run() {
    $JUG_RUN "$SCRIPT" $DASHDASH \
        --dataset "/p/vast1/LExperts/ComPile-Public" \
        --outdir "/l/ssd/$USER/compile-input-gen-out/" \
        --start "$START" \
        --end "$END" \
        --precompile-rts \
        --input-gen-runtime "$(readlink -f "$SCRIPT_DIR/../input-gen-runtimes/rt-input-gen.cpp")" \
        --input-run-runtime "$(readlink -f "$SCRIPT_DIR/../input-gen-runtimes/rt-run.cpp")" \
        --input-gen-num 1 \
        --input-gen-num-retries 5 \
        --input-gen-timeout 5 \
        --input-run-timeout 5 \
        --num-procs="$NUM_CPU" $ADDITIONAL_FLAGS
}
if [ "$JUG" != "" ]; then
    run
else
    run &> "$JOB_LOG"
fi
