#!/bin/bash

set -e
set -x

for p in shared memstore ctrl conn;
do
    pushd "$p"
        conan create .
    popd
done

for m in leader-election neb;
do
    pushd "$m"
        ./build.sh
    popd
done