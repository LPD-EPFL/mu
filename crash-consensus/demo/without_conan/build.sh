#!/bin/bash

set -e

rm -rf build
mkdir build
pushd build

cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
