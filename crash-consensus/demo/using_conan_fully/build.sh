#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
source $DIR/../profile.sh

cd $DIR

rm -rf build
mkdir build
pushd build

conan install .. --build missing
conan build ..
