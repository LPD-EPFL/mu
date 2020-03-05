# Memory Store

## Requirements

- [conan](https://conan.io/) package manager
- `libmemcached-dev` installed in the system libs

## Install

Run from within this folder:

```sh
conan create .
```

Which will create this package in the local conan cache.

## Usage

First, make sure to have the memcached server running under the default port `11211` and export the IP of the VM running the server under `DORY_REGISTRY_IP` at every client node.

E.g. when the memcached server is running on `lpdquatro1` you need to:

```sh
export DORY_REGISTRY_IP=128.178.154.41
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
