#!/bin/bash

# shellcheck disable=SC2087


REGISTRY_IP=128.178.154.41
USER="bruenn"
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
# shellcheck disable=SC2034
DATA_DIR="$DIR/data"
REMOTE_DIR="/home/bruenn/dory/neb"
REMOTE_OUT_FILE="/tmp/neb-bench-ts"
NODES=("lpdquatro1" "lpdquatro2" "lpdquatro3" "lpdquatro4")


host() {
    echo "$1.epfl.ch"
}

compile_neb() {
    echo "Recompiling neb in case of source change at ${NODES[0]}"

ssh "$USER@$(host "${NODES[0]}")" /bin/bash << "EOF"
    export PATH=$PATH:$HOME/.local/bin
    cd "$HOME/dory" || return 1
    ./build.py neb
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


# $1 = total number of nodes
# $2 = executable name
# $3 = total number of messages to send
run_nodes() {
    NUM_NODES=$(($1))
    BIN=$2
    NUM_MSG=$3
    for ((j=0; j < NUM_NODES; j++)); do
        echo "Starting NEB on ${NODES[$j]}"
        PID=$((j+1))
    
ssh "$USER@$(host "${NODES[$j]}")" /bin/bash << EOF
    tmux send-keys -t nebench "$BIN $PID $NUM_NODES $NUM_MSG" C-m
EOF

    done  
}


download_output() {
    NUM_NODES=$(($1))
    RESULTS_DIR="$DATA_DIR/$(date +%F)/$(date +%H:%M)"
    mkdir -p "$RESULTS_DIR"

    for ((j=0; j < NUM_NODES; j++)); do
        RESULTS_FILE_PATH="$RESULTS_DIR/${NODES[$j]}"
        scp $USER@"$(host "${NODES[$j]}")":$REMOTE_OUT_FILE "$RESULTS_FILE_PATH" &
    done 
}

teardown() {
    NUM_NODES=$(($1))

    for ((j=0; j < NUM_NODES; j++)); do
        ssh "$USER@$(host "${NODES[$j]}")" "tmux kill-session -t nebench"
    done
}
