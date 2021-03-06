#!/bin/bash

WAIT_TIME=30
MEMBERSHIP_FILE="./membership"
NUM_NODES=4
# ----------------------------- Argument Parsing ----------------------------- #
while [[ $# -gt 0 ]]
do
    key="$1"

    case $key in
        -k|--kill-before)
            KILL_BEFORE_BEGIN=true
            shift
        ;;
        -v|--validate)
            VALIDATE=true
            shift
        ;;
        -w|--wait)
            WAIT_TIME=$2
            shift
            shift
        ;;
        -n|--num-nodes)
            NUM_NODES=$2
            shift
            shift
        ;;
        -c|--compile)
            COMPILE=true
            shift
            shift
        ;;
        # default case
        *)
            # save it in an array for later restore
            POSITIONAL+=("$1")
            shift
        ;;
    esac
done
# restore positional parameters
set -- "${POSITIONAL[@]}"
# ---------------------------------------------------------------------------- #

source ./shared.sh

echo "Running with $NUM_NODES nodes and $NUM_MESSAGES messages per node"

if [[ $KILL_BEFORE_BEGIN ]]; then
    echo "Killing all sessions before setup"
    teardown $NUM_NODES
fi



start=$(date +%s)

if [[ $COMPILE  ]]; then
    compile_neb
fi

start_memcached

# 4 nodes with 20k messages each
create_membership "$MEMBERSHIP_FILE" $NUM_NODES 500000 500000 500000 500000

copy_membership "$MEMBERSHIP_FILE"

setup_nodes "$NUM_NODES"

run_nodes "$NUM_NODES" "./build/bin/main"

echo "Waiting for $WAIT_TIME sec"
sleep "$WAIT_TIME"

stop_nodes "$NUM_NODES"

echo "Downloading data samples"
RESULTS_DIR=$(download_output "$NUM_NODES")

teardown "$NUM_NODES"

stop_memcached

end=$(date +%s)

echo "Took" $((end-start)) "seconds to run"

if [[ $VALIDATE ]]; then 
    echo "Finally, validating output."
    ./validate.py "$RESULTS_DIR"
fi 