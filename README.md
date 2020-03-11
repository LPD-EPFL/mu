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
- cmake
- clang-format v6.0.0

## Build

Run (with `gcc` as default compiler) from within the root:

```sh
./build.sh
```

this will create all conan packages and build the executables.

---

You can also build with `clang` (fixed to v6.0) by calling `build.sh clang`.
Make sure to have:

```sh
export CC=/path/to/clang
export CXX=/path/to/clang++
```

in the env (required by cmake), otherwise the script will exit.

## Usage

Refer to the respective package READMEs.
