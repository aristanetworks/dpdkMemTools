CC=g++
CFLAGS=-g -I/usr/include/dpdk -O2

DPDK_LIB_NAME := $(if $(DPDK_LIB_NAME),$(DPDK_LIB_NAME),dpdk)
%.out: %.cpp
	$(CC) -o $@ $< $(CFLAGS) -g -fPIC -march=native -l$(DPDK_LIB_NAME) 

all: testMallocTrack.out

.PHONY: clean
clean:
	rm -fr *.*out
