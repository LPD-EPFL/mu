#!/bin/bash
set -x
set -e

rm -rf build
mkdir build
pushd build

conan install .. --build=missing

cmake .. && cmake --build .
