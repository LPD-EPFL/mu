LD      := g++
LDFLAGS := ${LDFLAGS} -libverbs -lrt -lpthread -lmemcached -lnuma
CXXFLAGS := -g -std=c++17 -O3 -Wall -Werror -Wno-unused-result 
BUILDDIR := build

APPS    := main

all: ${APPS}

main: buffer_overlay.o ctrl_block.o store_conn.o util.o neb.o main.o
	${LD} -o $@ $^ ${LDFLAGS}

PHONY: clean
clean:
	rm -f *.o ${APPS}
