CC=g++
CFLAGS=-g -I/usr/include/dpdk -O2

%.o: %.cpp
	$(CC) -c -o $@ $< $(CFLAGS) -fPIC

libRteMpTrack.so: rteMpTrack.o
	$(CC) -o $@ $< -fPIC -shared $(CFLAGS)

.PHONY: clean
clean:
	rm -fr *.*o
