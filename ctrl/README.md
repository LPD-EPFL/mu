# Control

## Requirements

- [conan](https://conan.io/) package manager

## Install

Run from within this folder:

```sh
conan create .
```

Which will create this package in the local conan cache.

## Usage


Inside a `conanfile.txt` specify:

```toml
[requires]
dory-ctrl/0.0.1

[options]
dory-ctrl:log_level=<level>
```

refer to our [wiki](https://github.com/kristianmitk/dory/wiki/Logger) to
see the various log level options.

Use the lib in the source files as follows:

```cpp
#include <dory/ctrl/ae_handler.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/ctrl/device.hpp>

```
