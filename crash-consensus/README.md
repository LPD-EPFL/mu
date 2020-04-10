# Consensus fast-path

## Requirements

- [conan](https://conan.io/) package manager

## Build

From within root:

```sh
./build.sh
```

## Usage

From within root:

```sh
export DORY_REGISTRY_IP=<memcached-ip>:<memcached-port>
export MLX_QP_ALLOC_TYPE="HUGE"
export MLX_CQ_ALLOC_TYPE="HUGE"
# export MLX4_SINGLE_THREADED=1
# export MLX5_SINGLE_THREADED=1

numactl --membind=0 -- ./build/bin/main <process-id>
```

_Note:_ process IDs have to start from 0.
