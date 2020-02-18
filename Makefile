FLAGS  := -std=c++17 -O3 -Wall -Werror -Wno-unused-result
LD      := g++
LDFLAGS := ${LDFLAGS} -libverbs -lrt -lpthread -lmemcached -lnuma
CXXFLAGS := -g 

APPS    := main

all: ${APPS}

main: hrd_conn.o hrd_util.o neb.o main.o
	${LD} -o $@ $^ ${LDFLAGS}

PHONY: clean
clean:
	rm -f *.o ${APPS}
