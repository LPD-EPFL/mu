#!/bin/bash

. deploy_common.sh

tmux_cancel_windows() {
  # for win in $(tmux list-windows -t $1 -F '#W'); do
  #   tmux send-keys -t $1:$win C-c;
  # done
  tmux send-keys -t $1:0 C-c;
}

# root_pid=$(tmux list-pane -t $session -F "#{pane_pid}")
# pids=$(list_descendants $root_pid)
# if [ -n "$pids" ]
# then
#   kill -9 $pids
# fi

tmux_cancel_windows $session
tmux kill-session -t $session 2>/dev/null
