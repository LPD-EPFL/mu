From ubuntu:bionic

RUN apt-get update && apt-get -y install libibverbs-dev libmemcached-dev python3 python3-pip cmake ninja-build clang lld clang-format
RUN pip3 install --upgrade conan
RUN conan profile new default --detect
RUN conan profile update settings.compiler.libcxx=libstdc++11 default
RUN apt-get update && apt-get -y install vim tmux git memcached libevent-dev libhugetlbfs-dev libgtest-dev libnuma-dev numactl libgflags-dev
RUN cd /usr/src/gtest && cmake CMakeLists.txt && make && make install

RUN apt-get install -y openssh-server
RUN mkdir /var/run/sshd

RUN sed -ri 's/UsePAM yes/#UsePAM yes/g' /etc/ssh/sshd_config

ENV DOCKERUSER=devuser
RUN adduser --disabled-password --shell /bin/bash --gecos "Docker User" $DOCKERUSER
RUN echo "$DOCKERUSER:<dockeruser-password>" |chpasswd

EXPOSE 22

CMD    ["/usr/sbin/sshd", "-D"]
