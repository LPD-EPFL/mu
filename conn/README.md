# Connection

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
dory-memstore/0.0.1
```

and use the lib in the source files as follows:

```cpp
#include <dory/conn/rc.hpp>
#include <dory/conn/exchanger.hpp>
```
