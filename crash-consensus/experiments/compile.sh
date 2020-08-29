#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $DIR

echo "Performing a clean compilation of crash-consensus"
../../build.py distclean
../../build.py crash-consensus

echo "Packaging crash-consensus in a shared library"
rm -rf ../shared-lib/build
../shared-lib/build.sh

rm -rf exported
cp -r ../shared-lib/build/lib exported
cp -r ../shared-lib/build/include exported
cp timers.h exported/include

LIB_PATH=`realpath $(pwd)/../shared-lib/build/lib`
export LIBRARY_PATH=$LIB_PATH:$LIBRARY_PATH

echo "Building redis"
cd redis
rm -rf bin
mkdir bin

rm -rf redis-2.8.24
tar xf redis-2.8.24.tar.gz
make -C redis-2.8.24 -j
mv redis-2.8.24/src/redis-server bin
mv redis-2.8.24/src/redis-cli bin
make -C redis-2.8.24 clean

patch -p1 -d redis-2.8.24 < redis-2.8.24.patch
make -C redis-2.8.24 -j
mv redis-2.8.24/src/redis-server bin/redis-server-replicated

cd latency_test
rm -rf build
mkdir build
cd build
cmake ..
cmake --build .
mv redis-puts-only ../../bin
mv redis-gets-only ../../bin
mv redis-puts-gets ../../bin
cd ../../../

echo "Building memcached"
cd memcached
rm -rf bin
mkdir bin

rm -rf memcached-1.5.19
tar xf memcached-1.5.19.tar.gz
cd memcached-1.5.19

./configure
make -j
mv memcached ../bin

cd ..
patch -p1 -d memcached-1.5.19 < memcached-1.5.19.patch
cd memcached-1.5.19
sed -i "s/LIBS = -lhugetlbfs -levent/LIBS = -lhugetlbfs -levent\nLIBS += -lconsensus/" Makefile
make -j
mv memcached ../bin/memcached-replicated
cd ../

cd latency_test
rm -rf build
mkdir build
cd build
cmake ..
cmake --build .
mv memcached-puts-only ../../bin
mv memcached-gets-only ../../bin
mv memcached-puts-gets ../../bin
cd ../../../

echo -e -n "\e[33m"
echo "Before using the generated binaries, run:"
echo -e -n "\e[1m"
echo "export LD_LIBRARY_PATH=$LIB_PATH:\$LD_LIBRARY_PATH"
echo -e -n "\e[21m"
echo -e -n "\e[39m"