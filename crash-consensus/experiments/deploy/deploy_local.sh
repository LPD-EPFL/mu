#!/bin/bash

. deploy_common.sh

# Check if the session exists, discarding output
# We can check $? for the exit status (zero for success, non-zero for failure)
tmux has-session -t $session 2>/dev/null

if [ $? == 0 ]; then
    tmux send-keys -t $session C-c
else
    tmux new-session -d -s $session
fi

tmux send-keys -t $session "eval $(./generate_exports.py $NUM_INSTANCES $DORY_REG_IP $STARTING_PORT $INSTANCE_NUM)" C-m
tmux send-keys -t $session "export LD_LIBRARY_PATH=$SHARED_LIB_PATH:\$LD_LIBRARY_PATH" C-m
tmux send-keys -t $session "cd $BIN_PATH" C-m
tmux send-keys -t $session "eval $COMMAND" C-m