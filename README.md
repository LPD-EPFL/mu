# Dory

#### [Wiki](https://github.com/kristianmitk/dory/wiki)

[![CircleCI](https://circleci.com/gh/kristianmitk/dory/tree/master.svg?style=shield&circle-token=8aee442f89261c33ece50901b09ef414a085ca9f)](https://circleci.com/gh/kristianmitk/dory/tree/master) ![clang-format-test](https://github.com/kristianmitk/dory/workflows/clang-format-test/badge.svg)

## Requirements

- [conan](https://conan.io/) package manager
    ```sh 
    pip3 install --user conan
    ```

    make sure to set the default ABI to C++11 with:

    ```sh
    conan profile new default --detect  # Generates default profile detecting GCC and sets old ABI
    conan profile update settings.compiler.libcxx=libstdc++11 default  # Sets libcxx to C++11 ABI
    ```

- libmemcached-dev
- libibverbs-dev
- clang-format v6.0.0



## Build

Run from within the root:

```sh
./build.sh
```

this will create all conan packages and build the executables.

## Usage

Refer to the respective package READMEs.
