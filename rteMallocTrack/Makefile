CC=g++
CFLAGS=-g -I/usr/include/dpdk -O2

%.o: %.cpp
	$(CC) -c -o $@ $< $(CFLAGS) -fPIC

DPDK_LIB_NAME := $(if $(DPDK_LIB_NAME),$(DPDK_LIB_NAME),dpdk)
libRteMallocTrack.so: rteMallocTrack.o
	$(CC) -o $@ $< -fPIC -shared $(CFLAGS) -l$(DPDK_LIB_NAME)

.PHONY: clean
clean:
	rm -fr *.*o
