#!/bin/bash

NUM_NODES=3
NUM_MESSAGES=5000
# ----------------------------- Argument Parsing ----------------------------- #
while [[ $# -gt 0 ]]
do
    key="$1"

    case $key in
        -n|--num-nodes)
            NUM_NODES=$2
            shift
            shift
        ;;
        -m|--num-messages)
            NUM_MESSAGES=$2
            shift
            shift
        ;;
        -k|--kill-before)
            KILL_BEFORE_BEGIN=true
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

compile_neb

start_memcached

setup_nodes "$NUM_NODES"

run_nodes "$NUM_NODES" "./build/bin/main" "$NUM_MESSAGES"

echo "Waiting for 30 sec"
sleep 30

echo "Downloading data samples"
download_output "$NUM_NODES"

teardown "$NUM_NODES"

end=$(date +%s)

echo "Took" $((end-start)) "seconds to run"