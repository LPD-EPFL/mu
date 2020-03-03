# Non-Equivocating Broadcast with RDMA

[![CircleCI](https://circleci.com/gh/kristianmitk/neb-rdma.svg?style=shield&circle-token=8aee442f89261c33ece50901b09ef414a085ca9f)](https://circleci.com/gh/kristianmitk/neb-rdma)

## Requirements

- [conan](https://conan.io/) package manager
- ibverbs
- memcached

## Build

From within root:

```sh
./build.sh
```

## Usage

From within root:

```sh
./build/bin/main <process-id> <numer-of-processes>
```

_Note:_ process IDs have to start from 0.
