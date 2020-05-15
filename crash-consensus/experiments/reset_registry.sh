#!/bin/bash

session="dory_registry"
DORY_REGISTRY_PORT=9999

# Check if the session exists, discarding output
# We can check $? for the exit status (zero for success, non-zero for failure)
tmux has-session -t $session 2>/dev/null

if [ $? == 0 ]; then
    tmux send-keys -t dory_registry C-c
else
    tmux new-session -d -s $session
fi

tmux send-keys -t dory_registry "memcached -vv -p $DORY_REGISTRY_PORT" C-m
