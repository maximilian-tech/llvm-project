#!/usr/bin/env bash

set -e
set -x

module load python/3.10.8

START=0
END=300000

NODES="$1"
TIMEOUT="$2"
JOB_LOG="$3"

NUM_CPU=40

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
RAY_OUT=$(ray start --head --num-cpus="$NUM_CPU")

COMMAND="$(echo $RAY_OUT | grep -o -- "ray start --address='.*'")"

echo $COMMAND --num-cpus="$NUM_CPU"


# TODO INSTALL INPUT GEN HERE
. enable.sh "/usr/WS1/$USER/opt/input-gen-release"

NUM_CHILD_NODES=$(("$NODES" - 1))
flux submit -N "$NUM_CHILD_NODES" -x -t "$TIMEOUT" --output="$JOB_LOG" $COMMAND --num-cpus="$NUM_CPU"

while [ "$(ray status  | grep -- ' node_' | wc -l)" != "$NODES" ]; do
    sleep 5s
done

"$SCRIPT_DIR/mass_input_gen.py" --dataset /p/vast1/LExperts/ComPile-Public --outdir "/l/ssd/$USER/compile-input-gen-out/" --start "$START" --end "$END" --precompile-rts --use-ray
