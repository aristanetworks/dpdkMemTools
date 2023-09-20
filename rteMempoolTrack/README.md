About
=====
rteMempoolTrack is a utility to override the DPDK mempool ops to intercept the mempool alloc and free and track them.
It has capability to track - 
1. Stray alloc( Basically an alloc from the mempool before it is populated )
2. Stray free
3. Duplicate free
4. Dumping the current allocations OR the whole mempool on the receipt of SIGRTMIN signal.

The allocations and free tracking happens along with a configurable number of backtrace frames( environment variable RTE_MP_TRACK_NUM_BT ).

The utility is built as a library which can be LD_PRELOADed to interpose the symbol rte_mempool_set_ops_byname and set a custom rte_mempool_ops for the mempool to be tracked.

The custom rte_mempool_ops of enqueue and dequeue is used to track the alloc after calling the original enqueue function and track the free before calling the original dequeue function respectively.

Note: The below been tried out only on a Linux distribution running on x86-64 platform with DPDK installed as a shared library

Building the Library
====================
```
% cd ./rteMempoolTrack
% make
g++ -c -o rteMpTrack.o rteMpTrack.cpp -g -I/usr/include/dpdk -O2 -fPIC
g++ -o libRteMpTrack.so rteMpTrack.o -fPIC -shared -g -I/usr/include/dpdk -O2 -ldpdk

% file libRteMpTrack.so
libRteMpTrack.so: ELF 64-bit LSB shared object, x86-64, version 1 (GNU/Linux), dynamically linked, BuildID[sha1]=66112cbb527ffeeac0f7c181207568fbac37be38, not stripped
```

Note: This assumes that the DPDK shared library name is "dpdk". If it is something else, set the environment variable DPDK_LIB_NAME before running the make.
For example, if libfoo.so is the shared library containing DPDK functionality -
```
% DPDK_LIB_NAME=foo make
g++ -c -o rteMpTrack.o rteMpTrack.cpp -g -I/usr/include/dpdk -O2 -fPIC
g++ -o libRteMpTrack.so rteMpTrack.o -fPIC -shared -g -I/usr/include/dpdk -O2 -lfoo
```

Building the test utility
=========================
```
% cd ./rteMempoolTrack/test

% make
g++ -o testMpTrack.out testMpTrack.cpp -g -I/usr/include/dpdk -O2 -g -fPIC -march=native -ldpdk

% file testMpTrack.out
testMpTrack.out: ELF 64-bit LSB executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, for GNU/Linux 2.6.32, BuildID[sha1]=e45316659a846554c2de8c09d5448db5dfa46527, not stripped
```
Note: This assumes that the DPDK shared library name is "dpdk". If it is something else, set the environment variable DPDK_LIB_NAME before running the make.

For example, if libfoo.so is the shared library containing DPDK functionality -
```
% DPDK_LIB_NAME=foo make
g++ -o testMpTrack.out testMpTrack.cpp -g -I/usr/include/dpdk -O2 -g -fPIC -march=native -lfoo
```

Running and Testing
===================

Note: Set the enviroment variable RTE_MP_TRACK_DPDK_LIBNAME to the name of the shared library containing DPDK functionality. In this example, the DPDK shared library name is assumed to be libdpdk.so

```
% RTE_MP_TRACK_DPDK_LIBNAME=libdpdk.so RTE_MP_TRACK_NUM_BT=10 RTE_MP_TRACK_NAME=mpTrackPool LD_PRELOAD=../libRteMpTrack.so ./testMpTrack.out --no-huge -m 1024

EAL: SSE4 support detected
EAL: Detected CPU lcores: 96
EAL: Detected NUMA nodes: 2
EAL: Detected static linkage of DPDK
EAL: Multi-process socket /tmp/dpdk/rte/mp_socket
EAL: Selected IOVA mode 'VA'
rte_eal_init done. Press any key to create the mempool
<ENTER>
Dumping contents of the /tmp/mpTrackMapDump.txt file:

Addr,Alloc-Backtrace,Free-Backtrace

Done!

rte_pktmbuf_pool_create done. Press any key to alloc
<ENTER>

Dumping contents of the /tmp/mpTrackMapDump.txt file:

Addr,Alloc-Backtrace,Free-Backtrace

Done!

rte_pktmbuf_alloc done. Press any key to alloc again
<ENTER>

Dumping contents of the /tmp/mpTrackMapDump.txt file:

Addr,Alloc-Backtrace,Free-Backtrace
0x140aa57c0,../libRteMpTrack.so(+0x2436) [0x7ff6cbf67436] ./testMpTrack.out() [0x401c03] ./testMpTrack.out(main+0x6d) [0x40148d] /lib64/libc.so.6(__libc_start_main+0x106) [0x7ff6c5b75306] ./testMpTrack.out() [0x4015cf],<NA>

Done!

rte_pktmbuf_alloc done. Press any key to free
<ENTER>

Dumping contents of the /tmp/mpTrackMapDump.txt file:

Addr,Alloc-Backtrace,Free-Backtrace
0x140aa5880,../libRteMpTrack.so(+0x2436) [0x7ff6cbf67436] ./testMpTrack.out() [0x401c03] ./testMpTrack.out(main+0x8a) [0x4014aa] /lib64/libc.so.6(__libc_start_main+0x106) [0x7ff6c5b75306] ./testMpTrack.out() [0x4015cf],<NA>
0x140aa57c0,../libRteMpTrack.so(+0x2436) [0x7ff6cbf67436] ./testMpTrack.out() [0x401c03] ./testMpTrack.out(main+0x6d) [0x40148d] /lib64/libc.so.6(__libc_start_main+0x106) [0x7ff6c5b75306] ./testMpTrack.out() [0x4015cf],<NA>

Done!

rte_pktmbuf_free done. Press any key to free again

<ENTER>

Dumping contents of the /tmp/mpTrackMapDump.txt file:

Addr,Alloc-Backtrace,Free-Backtrace
0x140aa57c0,../libRteMpTrack.so(+0x2436) [0x7ff6cbf67436] ./testMpTrack.out() [0x401c03] ./testMpTrack.out(main+0x6d) [0x40148d] /lib64/libc.so.6(__libc_start_main+0x106) [0x7ff6c5b75306] ./testMpTrack.out() [0x4015cf],<NA>

Done!

mp:mpTrackPool Duplicate free of 0x140aa5880 is not expected.Already freed at:../libRteMpTrack.so(+0x3785) [0x7ff6cbf68785]
./testMpTrack.out() [0x401ac3]
./testMpTrack.out(main+0xa6) [0x4014c6]
/lib64/libc.so.6(__libc_start_main+0x106) [0x7ff6c5b75306]
./testMpTrack.out() [0x4015cf]

testMpTrack.out: rteMpTrack.cpp:46: void fatal(const char*, ...): Assertion `0' failed.
Aborted (core dumped)
```

