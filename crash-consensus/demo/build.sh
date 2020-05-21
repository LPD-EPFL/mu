#!/bin/bash

set -e

rm -rf build
mkdir build
pushd build

cmake ..
cmake --build .
