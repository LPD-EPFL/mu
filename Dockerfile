From ubuntu:bionic

RUN apt-get update && apt-get -y install libibverbs-dev libmemcached-dev python3 python3-pip cmake
RUN pip3 install --upgrade conan
RUN conan profile new default --detect
RUN conan profile update settings.compiler.libcxx=libstdc++11 default
