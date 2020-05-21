# Consensus fast-path

## Requirements

- [conan](https://conan.io/) package manager

## Build

From within root:

```sh
# Create the static library and store it in the exported sub-dir
cd crash-consensus
export.sh

cd ..

cd crash-consensus-demo
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
