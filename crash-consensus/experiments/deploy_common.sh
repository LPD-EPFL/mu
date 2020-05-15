#!/bin/bash

NUM_INSTANCES=$1
DORY_REG_IP=$2
STARTING_PORT=$3
INSTANCE_NUM=$4

list_descendants ()
{
  local children=$(ps -o pid= --ppid "$1")

  for pid in $children
  do
    list_descendants "$pid"
  done

  echo "$children"
}


# BIN_PATH=~/dory/crash-consensus/experiments/memcached/bin
# SHARED_LIB_PATH=~/dory/crash-consensus/shared-lib/build/lib

# session="memcached_exper_$INSTANCE_NUM"
# COMMAND='numactl -C 0,12,14,16,18 --membind 0 -- ./memcached-replicated -p $EXPER_PORT'

BIN_PATH=~/dory/crash-consensus/experiments/redis/bin
SHARED_LIB_PATH=~/dory/crash-consensus/shared-lib/build/lib

session="redis_exper_$INSTANCE_NUM"
COMMAND='numactl -C 0,12,14,16,18 --membind 0 -- ./redis-server-replicated --port $EXPER_PORT'