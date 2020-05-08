#!/bin/bash

# shellcheck disable=SC2087

REGISTRY_IP=128.178.154.41
USER="bruenn"
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
# shellcheck disable=SC2034
DATA_DIR="$DIR/data"
REMOTE_DIR="/home/bruenn/dory/neb"
THROUGHPUT_REMOTE_OUT_FILE="/tmp/neb-bench-ts"
LATENCY_REMOTE_OUT_FILE="/tmp/neb-bench-lat"
NODES=("lpdquatro1" "lpdquatro2" "lpdquatro3" "lpdquatro4")

host() {
    echo "$1.epfl.ch"
}

compile_neb() {
    echo "Recompiling neb in case of source change at ${NODES[0]}"

ssh "$USER@$(host "${NODES[0]}")" /bin/bash << "EOF"
    export PATH=$PATH:$HOME/.local/bin
    export CONAN_USER_HOME="/localhome/bruenn"
    cd "$HOME/dory/neb" || return 1
    ./build-executable.sh
EOF
}

start_memcached() {

ssh "$USER@$REGISTRY_IP" /bin/bash << EOF
    tmux start-server
    tmux kill-session -t memcached 2> /dev/null
    killall memcached 2> /dev/null
    tmux new-session -d -s memcached
    tmux send-keys -t memcached "memcached -vv" C-m
EOF

echo "Started memcached on $REGISTRY_IP"
}

stop_memcached() {
    ssh "$USER@$REGISTRY_IP" "killall memcached 2> /dev/null; tmux kill-session -t memcached"
    echo "Stopped memcached on $REGISTRY_IP"
}

setup_nodes() {
    NUM_NODES=$(($1))
    for ((j=0; j < NUM_NODES; j++)); do
        echo "Setting up ${NODES[$j]}"

ssh "$USER@$(host "${NODES[$j]}")" /bin/bash << EOF
    tmux start-server
    tmux kill-session -t nebench 2> /dev/null
    cd $REMOTE_DIR
    tmux new-session -d -s nebench
    tmux send-keys -t nebench "export DORY_REGISTRY_IP=$REGISTRY_IP" C-m
EOF

    done
}


create_membership() {
    MEMBERSHIP_FILE=$1
    NUM_NODES=$(($2))
    echo $NUM_NODES > "$MEMBERSHIP_FILE"
    shift
    shift

    argc=$#
    argv=("$@")
    for ((j=0; j < argc; j++)); do
        echo "$((j+1)) ${argv[j]}" >> "$MEMBERSHIP_FILE"
    done
}

copy_membership() {
    MEMBERSHIP_FILE=$1
    echo "Using following membership for this benchmark"
    cat "$MEMBERSHIP_FILE"
    scp "$MEMBERSHIP_FILE" $USER@"$(host "${NODES[0]}"):~/dory/neb/membership"
}

# $1 = total number of nodes
# $2 = executable name
# $3 = total number of messages to send
run_nodes() {
    NUM_NODES=$(($1))
    BIN=$2
    for ((j=0; j < NUM_NODES; j++)); do
        echo "Starting NEB on ${NODES[$j]}"
        PID=$((j+1))
    
ssh "$USER@$(host "${NODES[$j]}")" /bin/bash << EOF
    tmux send-keys -t nebench "$BIN $PID" C-m
EOF

    done  
}

download_output() {
    NUM_NODES=$(($1))
    RESULTS_DIR="$DATA_DIR/$(date +%F)/$(date +%H:%M)"
    mkdir -p "$RESULTS_DIR"
    
    scp $USER@"$(host "${NODES[0]}"):~/dory/neb/membership" "$RESULTS_DIR/membership"

    for ((j=0; j < NUM_NODES; j++)); do
        THROUGHPUT_RESULTS_FILE_PATH="$RESULTS_DIR/${NODES[$j]}.out"
        LAT_RESULTS_FILE_PATH="$RESULTS_DIR/${NODES[$j]}.lat"
        scp $USER@"$(host "${NODES[$j]}")":$THROUGHPUT_REMOTE_OUT_FILE "$THROUGHPUT_RESULTS_FILE_PATH"
        scp $USER@"$(host "${NODES[$j]}")":$LATENCY_REMOTE_OUT_FILE "$LAT_RESULTS_FILE_PATH"
    done 
}

stop_nodes() {
   NUM_NODES=$(($1))
    for ((j=0; j < NUM_NODES; j++)); do
        echo "Stopping NEB on ${NODES[$j]}"
    
ssh "$USER@$(host "${NODES[$j]}")" /bin/bash << EOF
    tmux send-keys -t nebench C-c
EOF

    done   
}

teardown() {
    NUM_NODES=$(($1))

    for ((j=0; j < NUM_NODES; j++)); do
        ssh "$USER@$(host "${NODES[$j]}")" "tmux kill-session -t nebench; rm $THROUGHPUT_REMOTE_OUT_FILE; rm $LATENCY_REMOTE_OUT_FILE"
        echo "Killed session at ${NODES[$j]}"
    done
}
