#!/bin/bash

# FIRST EDIT THE deploy_common.sh

# We pick these numbers, such that the replication buffer is 64, 128, 256 and 512 bytes long

key_size=8

for value_size in 30 94 221 477
do
    ./deploy_remote.sh start
    sleep 60

    redis/bin/redis-puts-only $key_size $value_size lpdquatro1 6379
    sleep 5

    ./deploy_remote.sh first
    sleep 5

    mv redis-cli.txt ~/dory/measurements/redis/3/redis-cli-puts-only-$key_size-$value_size.txt
    mv redis/bin/dump-1.txt ~/dory/measurements/redis/3/redis-puts-only-$key_size-$value_size.txt
    sleep 5

    ./deploy_remote.sh stop
    sleep 5
done

for value_size in 30 94 221 477
do
    ./deploy_remote.sh start
    sleep 60

    redis/bin/redis-gets-only $key_size $value_size lpdquatro1 6379
    sleep 5

    ./deploy_remote.sh first
    sleep 5

    mv redis-cli.txt ~/dory/measurements/redis/3/redis-cli-gets-only-$key_size-$value_size.txt
    mv redis/bin/dump-1.txt ~/dory/measurements/redis/3/redis-gets-only-$key_size-$value_size.txt
    sleep 5

    ./deploy_remote.sh stop
    sleep 5
done

for value_size in 30 94 221 477
do
    ./deploy_remote.sh start
    sleep 60

    redis/bin/redis-puts-gets $key_size $value_size lpdquatro1 6379
    sleep 5

    ./deploy_remote.sh first
    sleep 5

    mv redis-cli.txt ~/dory/measurements/redis/3/redis-cli-puts-gets-$key_size-$value_size.txt
    mv redis/bin/dump-1.txt ~/dory/measurements/redis/3/redis-puts-gets-$key_size-$value_size.txt
    sleep 5

    ./deploy_remote.sh stop
    sleep 5
done