#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $DIR

./package_orig_liquibook.sh

git clone https://github.com/erpc-io/eRPC
cd eRPC
git checkout 2ab3bc739ea39a2a7b0c9800dedfc40b957837b8
cd ..
mkdir eRPC/apps/liquibook
patch -p1 -d eRPC < step1.patch
tar xf liquibook_orig.tar.gz -C eRPC/apps/liquibook/
tar xf liquibook_additions.tar.gz -C eRPC/apps/liquibook/
patch -p1 -d eRPC/apps/liquibook < step2.patch
patch -p1 -d eRPC/apps/liquibook < step3.patch
patch -p1 -d eRPC < step4.patch
echo liquibook > eRPC/scripts/autorun_app_file

cd eRPC
cp -r ../../../libgen/exported/ apps/liquibook/dory-export
cmake . -DPERF=ON -DTRANSPORT=infiniband
make -j
