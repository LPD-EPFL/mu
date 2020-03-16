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
dory-crtl/0.0.1
```

and use the lib in the source files as follows:

```cpp
#include <dory/ctrl/ae_handler.hpp>
#include <dory/ctrl/block.hpp>
#include <dory/ctrl/device.hpp>

```
