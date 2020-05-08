# Non-Equivocating Broadcast

## Requirements

- [conan](https://conan.io/) package manager

## Build

### Executable

From within root:

```sh
./build-executable.sh
```

### Membership file

a `membership` file specifies the cluster membership.

The first line specifies the total number of processes. Subsequent lines provide
the process id followed by the number of messages to be broadcast by that process.

For example:

```
3
1 1000
2 500
3 5000
4 1000
```

will create a run with 3 processes (1-3). Note: process 4 in the membership file
will be ignored as the first line specified 3 as the total number of processes.

### Run

From within root:

```sh
./build/bin/main <process-id>
```

## Package

```sh
conan create .
```

### Usage

```toml
[requires]
dory-neb/0.0.1

[options]
dory-neb:log_level=<level>
```

refer to our [wiki](https://github.com/kristianmitk/dory/wiki/Logger) to
see the various log level options.

Use the lib in the source files as follows:

```cpp
#include <dory/neb/sync.hpp>
#include <dory/neb/broadcastable.hpp>
#include <dory/neb/consts.hpp>
```
