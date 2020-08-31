#!/bin/bash

. deploy_common.sh

root_pid=$(tmux list-pane -t $session -F "#{pane_pid}")
pids=$(list_descendants $root_pid)

kill -SIGUSR1 $pids