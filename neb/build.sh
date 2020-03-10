#!/bin/bash

set -e

rm -rf build
mkdir build
pushd build

if [ -z "$1" ]; then
    COMPILER="gcc"
else
    COMPILER="$1"
fi

if [ "$COMPILER" == "clang" ]; then
    conan install .. -s compiler=clang -s compiler.version="6.0" -s compiler.libcxx="libstdc++11" --build missing
elif [ "$COMPILER" == "gcc" ]; then
    conan install ..
else
    echo "Unsupported compiler: $COMPILER"
    exit 1;
fi

cmake ..
cmake --build .
