# Memory Store

## Requirements

- [conan](https://conan.io/) package manager

## Install

Run from within this folder:

```sh
conan create .
```

Which will create this package in the local conan cache.

## Usage

First, make sure to have the memcached server running. If not specified otherwise, the memcached server is running under the default port `11211`. Export the IP or domain name of the VM running the server under `DORY_REGISTRY_IP` at every client node. The general syntax is:

```sh
export DORY_REGISTRY_IP=DomainOrIP[:Port]
```

E.g. when the memcached server is running on `example.com`, port `9999` you need to:

```sh
export DORY_REGISTRY_IP=example.com:9999
```

Then, inside a `conanfile.txt` specify:

```toml
[requires]
dory-memstore/0.0.1
```

and use the lib in the source files as follows:

```cpp
#include <dory/store.hpp>

dory::MemoryStore::getInstance();
```
