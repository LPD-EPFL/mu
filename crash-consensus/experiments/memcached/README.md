# Crash-Consensus Memcached Experiment

## Important
Memcached will not work if you have libev install in the system,
after libevent was previously installed.
The reason is that the file in ~/.local/include/event.h is replaced by libev.
Memcached gets confused by the existence of this file and sets some defines
in a bogus way during compilation.


## Requirements
First apply the patch:

```sh
patch -p1 -d memcached-1.5.19 < memcached-1.5.19.patch
```

Then compile:

```sh
export LIBRARY_PATH=/path/to/libconsensus.so
export LD_LIBRARY_PATH=/path/to/libconsensus.so
cd memcached-1.5.19
./configure
sed -i "s/LIBS =  -levent/LIBS =  -levent\nLIBS += lconsensus/" Makefile
make -j
```

```sh
export SID=<process id corresponding to consensus>
export IDS=<process ids corresponding to all consensus instances>
export DORY_REGISTRY_IP=<hostname>:<port>
```

Finally run:
```sh
./memcached-1.5.19/memcached
```

## Puts and Gets
We can use telnet to submit simple puts and gets. For example:
```sh
telnet localhost 11222
```

For a PUT, type:
```sh
set <key> 0 0 <length_of_value>
<value>
```
, where `<key>`, `<length_of_value>` and `<value>` are replaced by the user.
Notice, that it's two lines. After typing the first line, we hit enter and
type the second line as well.

For a GET, type:
```sh
get <key>
```
