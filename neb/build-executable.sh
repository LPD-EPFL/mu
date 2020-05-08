#!/bin/bash

set -e

rm -rf build
mkdir build
pushd build

conan install .. --build=outdated

cmake ../src \
    -DSPDLOG_ACTIVE_LEVEL:string=SPDLOG_LEVEL_WARN \
    -DBUILD_EXECUTABLE=True

cmake --build .
