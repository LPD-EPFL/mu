#!/bin/bash

set -e

LIB_NAME=libconsensus

rm -rf build
mkdir build

conan install . --install-folder=build/installation
conan build . --build-folder=build/installation
conan package . --build-folder=build/installation --package-folder=build/packaging

rm -rf exported
mkdir exported
cp -r build/packaging/include exported
cp build/installation/deps/* build/packaging/lib/

pushd build
mkdir archiving
pushd archiving
for dep in `ls ../packaging/lib`
do
    dep_dir="${dep%.*}"
    mkdir $dep_dir
    cp ../packaging/lib/$dep $dep_dir
    pushd $dep_dir
    ar x $dep
    rm $dep
    popd
    for object in `ls $dep_dir`
    do
        mv $dep_dir/$object ${dep_dir}_${object}
    done
    rm -r $dep_dir
done

ar rcs $LIB_NAME.a  *.o
popd
popd

mv build/archiving/$LIB_NAME.a exported
