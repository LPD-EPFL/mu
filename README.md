![alt text](logo/dory-logo-256.png "Dory")

*The word "dory" was first attested by Homer with the meanings of "wood" and "spear"*


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

Run from within the root:

```sh
./build.py
```

this will create all conan packages and build the executables.

__Note:__ `gcc` is used as the default compiler. You can also build with `clang` (fixed to v6.0) by calling `./build.py -c clang <target>`.


## Docker

You can pull the latest docker image satisfying the requirements from the registry:

```sh
docker pull kristianmitk/dory
```

or by manually building the [Dockerfile](https://github.com/kristianmitk/dory/blob/master/Dockerfile) under the root of this repo.

---


## Usage

Refer to the respective package READMEs.
