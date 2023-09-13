About
=====
rteMallocTrack is a utility to override the DPDK rte_malloc_socket() and rte_free() to intercept the RTE malloc and free and track them.
It has capability to track - 
1. Stray free
2. Duplicate free
3. Dumping the current allocations on the receipt of SIGRTMIN signal.

The allocations and free tracking happens is optional with the use RTE_MALLOC_TRACK_BACKTRACE environment variable.

The utility is built as a library which can be LD_PRELOADed to interpose the symbol rte_malloc_socket() and rte_free()

The custom rte_malloc_socket() and rte_free() is used to track the alloc after calling the original rte_malloc_socket() and track the rte_free() before calling the original rte_free() function respectively.

Note: The below been tried out only on a Linux distribution running on x86-64 platform with DPDK installed as a shared library

Building the Library
====================
```
% cd ./rteMallocTrack
% make
g++ -c -o rteMallocTrack.o rteMallocTrack.cpp -g -I/usr/include/dpdk -O2 -fPIC
g++ -o libRteMallocTrack.so rteMallocTrack.o -fPIC -shared -g -I/usr/include/dpdk -O2

% file libRteMallocTrack.so
libRteMallocTrack.so: ELF 64-bit LSB shared object, x86-64, version 1 (GNU/Linux), dynamically linked, BuildID[sha1]=3c09306ead47e59563df2270672dcd74f1a6da3e, not stripped

````
Building the test utility
=========================
```
% cd ./rteMallocTrack/test

% make
g++ -o testMallocTrack.out testMallocTrack.cpp -g -I/usr/include/dpdk -O2 -g -fPIC -march=native -ldpdk

% file testMallocTrack.out
testMallocTrack.out: ELF 64-bit LSB executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, for GNU/Linux 2.6.32, BuildID[sha1]=390c7275c4b28786ae539da3e8cda0e37af18e1c, not stripped
```
Note: This assumes that the DPDK shared library name is "dpdk". If it is something else, set the environment variable DPDK_LIB_NAME before running the make.

For example, if libfoo.so is the shared library containing DPDK functionality -
```
% DPDK_LIB_NAME=foo make
g++ -o testMallocTrack.out testMpTrack.cpp -g -I/usr/include/dpdk -O2 -g -fPIC -march=native -lfoo
```

Running and Testing
===================

Note: Set the enviroment variable RTE_MP_TRACK_DPDK_LIBNAME to the name of the shared library containing DPDK functionality. In this example, the DPDK shared library name is assumed to be libdpdk.so

```
% RTE_MALLOC_TRACK_DPDK_LIBNAME=libbess.so RTE_MALLOC_TRACK_BACKTRACE=1 LD_PRELOAD=../libRteMallocTrack.so ./testMallocTrack.out --no-huge -m 1024             EAL: SSE4 support detected
EAL: Detected CPU lcores: 96
EAL: Detected NUMA nodes: 2
EAL: Detected static linkage of DPDK
EAL: Multi-process socket /tmp/dpdk/rte/mp_socket
EAL: Selected IOVA mode 'VA'
rte_eal_init done. Press any key to malloc


Dumping contents of the /tmp/rteMallocTrackMapDump.txt file:

Addr,Size,Type,Backtrace
0x140c27f80,81920,rte_service_core_states,../libRteMallocTrack.so(rte_malloc_socket+0xeb) [0x7f012852d98b] /lib64/libbess.so(rte_zmalloc_socket+0x21) [0x7f0124c28211] /lib64/libbess.so(rte_service_init+0x61) [0x7f0124c2d481] /lib64/libbes
s.so(rte_eal_init+0x1640) [0x7f0124c37c90] ./testMallocTrack.out(main+0xd) [0x40131d] /lib64/libc.so.6(__libc_start_main+0x106) [0x7f012213a306] ./testMallocTrack.out() [0x40144f]                                                           0x140c3c000,8192,rte_services,../libRteMallocTrack.so(rte_malloc_socket+0xeb) [0x7f012852d98b] /lib64/libbess.so(rte_zmalloc_socket+0x21) [0x7f0124c28211] /lib64/libbess.so(rte_service_init+0x36) [0x7f0124c2d456] /lib64/libbess.so(rte_eal
_init+0x1640) [0x7f0124c37c90] ./testMallocTrack.out(main+0xd) [0x40131d] /lib64/libc.so.6(__libc_start_main+0x106) [0x7f012213a306] ./testMallocTrack.out() [0x40144f]
Done!

rte_malloc done. Press any key to free
<ENTER>

Dumping contents of the /tmp/rteMallocTrackMapDump.txt file:

Addr,Size,Type,Backtrace
0x140c27f80,81920,rte_service_core_states,../libRteMallocTrack.so(rte_malloc_socket+0xeb) [0x7f012852d98b] /lib64/libbess.so(rte_zmalloc_socket+0x21) [0x7f0124c28211] /lib64/libbess.so(rte_service_init+0x61) [0x7f0124c2d481] /lib64/libbes
s.so(rte_eal_init+0x1640) [0x7f0124c37c90] ./testMallocTrack.out(main+0xd) [0x40131d] /lib64/libc.so.6(__libc_start_main+0x106) [0x7f012213a306] ./testMallocTrack.out() [0x40144f]                                                           0x140c3c000,8192,rte_services,../libRteMallocTrack.so(rte_malloc_socket+0xeb) [0x7f012852d98b] /lib64/libbess.so(rte_zmalloc_socket+0x21) [0x7f0124c28211] /lib64/libbess.so(rte_service_init+0x36) [0x7f0124c2d456] /lib64/libbess.so(rte_eal
_init+0x1640) [0x7f0124c37c90] ./testMallocTrack.out(main+0xd) [0x40131d] /lib64/libc.so.6(__libc_start_main+0x106) [0x7f012213a306] ./testMallocTrack.out() [0x40144f]                                                                       0x140c27ec0,1,dummy,../libRteMallocTrack.so(rte_malloc_socket+0xeb) [0x7f012852d98b] ./testMallocTrack.out(main+0x34) [0x401344] /lib64/libc.so.6(__libc_start_main+0x106) [0x7f012213a306] ./testMallocTrack.out() [0x40144f]

Done!

rte_free done. Press any key to free again
<ENTER>

Dumping contents of the /tmp/rteMallocTrackMapDump.txt file:

Addr,Size,Type,Backtrace
0x140c27f80,81920,rte_service_core_states,../libRteMallocTrack.so(rte_malloc_socket+0xeb) [0x7f012852d98b] /lib64/libbess.so(rte_zmalloc_socket+0x21) [0x7f0124c28211] /lib64/libbess.so(rte_service_init+0x61) [0x7f0124c2d481] /lib64/libbes
s.so(rte_eal_init+0x1640) [0x7f0124c37c90] ./testMallocTrack.out(main+0xd) [0x40131d] /lib64/libc.so.6(__libc_start_main+0x106) [0x7f012213a306] ./testMallocTrack.out() [0x40144f]
0x140c3c000,8192,rte_services,../libRteMallocTrack.so(rte_malloc_socket+0xeb) [0x7f012852d98b] /lib64/libbess.so(rte_zmalloc_socket+0x21) [0x7f0124c28211] /lib64/libbess.so(rte_service_init+0x36) [0x7f0124c2d456] /lib64/libbess.so(rte_eal
_init+0x1640) [0x7f0124c37c90] ./testMallocTrack.out(main+0xd) [0x40131d] /lib64/libc.so.6(__libc_start_main+0x106) [0x7f012213a306] ./testMallocTrack.out() [0x40144f]

Done!

Stray free OR a duplicate free of address: 0x140c27ec0
Dumping the allocMap contents to the dumpFile
EAL: Error: Invalid memory

rte_free done. Press any key to do stray free
<ENTER>

Dumping contents of the /tmp/rteMallocTrackMapDump.txt file:

Addr,Size,Type,Backtrace
0x140c27f80,81920,rte_service_core_states,../libRteMallocTrack.so(rte_malloc_socket+0xeb) [0x7f012852d98b] /lib64/libbess.so(rte_zmalloc_socket+0x21) [0x7f0124c28211] /lib64/libbess.so(rte_service_init+0x61) [0x7f0124c2d481] /lib64/libbes
s.so(rte_eal_init+0x1640) [0x7f0124c37c90] ./testMallocTrack.out(main+0xd) [0x40131d] /lib64/libc.so.6(__libc_start_main+0x106) [0x7f012213a306] ./testMallocTrack.out() [0x40144f]
0x140c3c000,8192,rte_services,../libRteMallocTrack.so(rte_malloc_socket+0xeb) [0x7f012852d98b] /lib64/libbess.so(rte_zmalloc_socket+0x21) [0x7f0124c28211] /lib64/libbess.so(rte_service_init+0x36) [0x7f0124c2d456] /lib64/libbess.so(rte_eal
_init+0x1640) [0x7f0124c37c90] ./testMallocTrack.out(main+0xd) [0x40131d] /lib64/libc.so.6(__libc_start_main+0x106) [0x7f012213a306] ./testMallocTrack.out() [0x40144f]

Done!

Stray free OR a duplicate free of address: 0x1
Dumping the allocMap contents to the dumpFile
Segmentation fault (core dumped)
```
