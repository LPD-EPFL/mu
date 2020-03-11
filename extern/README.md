# External

This package contains external system header files used among the dory source.

## Requirements

- [conan](https://conan.io/) package manager
- libmemcached-dev
- libibverbs-dev

## Install

Run from within this folder:

```sh
conan create .
```

## Usage

Inside a `conanfile.txt` specify:

```toml
[requires]
dory-extern/0.0.1
```

and use in source files as follows:

```cpp
#include <dory/extern/ibverbs.hpp>
#include <dory/extern/memcached.hpp>
```
