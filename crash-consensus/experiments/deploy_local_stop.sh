#!/bin/bash

. deploy_common.sh

root_pid=$(tmux list-pane -t $session -F "#{pane_pid}")
pids=$(list_descendants $root_pid)
if [ -n "$pids" ]
then
  kill -9 $pids
fi
tmux kill-session -t $session 2>/dev/null
