#!/bin/bash

# FIRST EDIT THE deploy_common.sh

# We pick these numbers, such that the replication buffer is 64, 128, 256 and 512 bytes long

key_size=8

for value_size in 40 103 231 487
do
    ./deploy_remote.sh start
    sleep 60

    memcached/bin/memcached-puts-only $key_size $value_size lpdquatro1 6379
    sleep 5

    ./deploy_remote.sh first
    sleep 5

    mv memcached-cli.txt ~/dory/measurements/memcached/3/memcached-cli-puts-only-$key_size-$value_size.txt
    mv memcached/bin/dump-1.txt ~/dory/measurements/memcached/3/memcached-puts-only-$key_size-$value_size.txt
    sleep 5

    ./deploy_remote.sh stop
    sleep 5
done

for value_size in 40 103 231 487
do
    ./deploy_remote.sh start
    sleep 60

    memcached/bin/memcached-gets-only $key_size $value_size lpdquatro1 6379
    sleep 5

    ./deploy_remote.sh first
    sleep 5

    mv memcached-cli.txt ~/dory/measurements/memcached/3/memcached-cli-gets-only-$key_size-$value_size.txt
    mv memcached/bin/dump-1.txt ~/dory/measurements/memcached/3/memcached-gets-only-$key_size-$value_size.txt
    sleep 5

    ./deploy_remote.sh stop
    sleep 5
done

for value_size in 40 103 231 487
do
    ./deploy_remote.sh start
    sleep 60

    memcached/bin/memcached-puts-gets $key_size $value_size lpdquatro1 6379
    sleep 5

    ./deploy_remote.sh first
    sleep 5

    mv memcached-cli.txt ~/dory/measurements/memcached/3/memcached-cli-puts-gets-$key_size-$value_size.txt
    mv memcached/bin/dump-1.txt ~/dory/measurements/memcached/3/memcached-puts-gets-$key_size-$value_size.txt
    sleep 5

    ./deploy_remote.sh stop
    sleep 5
done
