# Shared

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
dory-shared/0.0.1
```

and use in source files as follows:

```cpp
#include <dory/shared/pointer-wrapper.hpp>
#include <dory/shared/unused_suppressor.hpp>
#include <dory/shared/logger.hpp>
#include <dory/shared/units.hpp>
#include <dory/shared/bench.hpp>

// For the units:
using namespace dory::units;
```
