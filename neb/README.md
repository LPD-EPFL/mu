# Non-Equivocating Broadcast

## Requirements

- [conan](https://conan.io/) package manager

## Build


### Package

Preferably use our python build script under the root of the repo and call it with:

```sh
./build.py neb
```

That script detects local dependency changes and re-builds them before building this module.

Alternatively from within this folder you can run:

```sh
conan create . --build=outdated
```


### Executable

From within root of this module:

```sh
./build-executable.sh --TARGET [SYNC|ASYNC] --LOG_LEVEL [...|INFO|WARN|...]
```

### Membership file

a `./membership` file specifies the cluster membership.

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

## Run

After building the executable as described in the previous section, run from within 
the root of this module:

```sh
./build/bin/main <process-id>
```

## Package Usage

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
#include <dory/neb/async.hpp>
#include <dory/neb/broadcastable.hpp>
#include <dory/neb/consts.hpp>
```

## Test

```sh
conan test ./test dory-neb/<version>
```

will build and run unit tests under `./test`