# Crash-Consensus Redis Experiment

## Requirements

First apply the patch:

```sh
patch -p1 -d redis-2.8.17 < redis-2.8.17.patch
```

Then compile:

```sh
export LIBRARY_PATH=/path/to/libconsensus.so
export LD_LIBRARY_PATH=/path/to/libconsensus.so
make -C redis-2.8.17 -j
```

```sh
export SID=<process id corresponding to consensus>
export IDS=<process ids corresponding to all consensus instances>
export DORY_REGISTRY_IP=<hostname>:<port>
```

Finally run:
```sh
./redis-2.8.17/src/redis-server
```
