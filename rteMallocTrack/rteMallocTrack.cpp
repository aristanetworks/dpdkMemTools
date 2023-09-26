/* Copyright © 2023 Arista Networks, Inc. All rights reserved.
 *
 * Use of this source code is governed by the MIT license that can be found
 * in the LICENSE file.
 */

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <execinfo.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <string>

#define PRINT_BACKTRACE \
do { \
  void * btAddrs[maxBtFrames]; \
  int btSize = backtrace(btAddrs, maxBtFrames); \
  backtrace_symbols_fd(btAddrs, btSize, 1); \
} while(0)


bool debugTrace = false; // Flip to en/disable debug prints
bool btEnable = false; // Flip to en/disable alloc backtrace
// Name of the library having the rte_malloc_socket and rte_free symbols
char *dpdkLibName;
void *dpdkLibHandle;
const uint16_t maxBtFrames = 1024;
typedef struct allocAttrib {
  const char * type;
  size_t size;
  void * btAddrs[maxBtFrames];
  int actualBtSize;
  std::string btSymsStr;
} allocAttrib_t;

std::unordered_map<void *, allocAttrib_t> allocMap;

typedef void* (*orig_rte_malloc_socket_t)(const char * type,
                                          size_t size,
                                          unsigned int align,
                                          int socket_arg);
typedef void* (*orig_rte_free_t)(void* addr);
orig_rte_malloc_socket_t orig_rte_malloc_socket;
orig_rte_free_t orig_rte_free;
void dumpAllocMapHandler(int signum);

inline void
debug(const char * format, ...) {
  if (debugTrace) {
    va_list argList;
    va_start(argList, format);
    vfprintf(stdout, format, argList);
    va_end(argList);
  }
}

inline void
verifyPerrorExit(bool condition, std::string errMsg) {
  if (!condition) {
    perror(errMsg.c_str());
    exit(1);
  }
}

extern "C" void *
rte_malloc_socket(const char * type,
             size_t size,
             unsigned int align,
             int socket_arg) {
  debug("rte_malloc_socket called for type:%s, size:%u\n", type, size);
  if (debugTrace)
    PRINT_BACKTRACE;
  if (!dpdkLibHandle) {
    verifyPerrorExit(dpdkLibName,
      "dpdkLibName is NULL, set it using environ RTE_MALLOC_TRACK_DPDK_LIBNAME");
    dpdkLibHandle = dlopen(dpdkLibName, RTLD_NOW);
    std::string errMsg = std::string("Failed to dlopen ") + dpdkLibName;
    verifyPerrorExit(dpdkLibHandle, errMsg);
  }
  if (!orig_rte_malloc_socket) {
    orig_rte_malloc_socket =
      (orig_rte_malloc_socket_t)dlsym(dpdkLibHandle, "rte_malloc_socket");
    std::string errMsg = "Failed to dlsym rte_malloc_socket";
    verifyPerrorExit(orig_rte_malloc_socket, errMsg);
  }
  void * allocPtr = orig_rte_malloc_socket(type, size, align, socket_arg);
  if (allocPtr) {
    if (!type) {
      type = "<unknown>";
    }
    allocAttrib_t allocAttrib = { type, size, {}, 0, "" };
    if (btEnable) {
      int btSize = backtrace(allocAttrib.btAddrs, maxBtFrames);
      std::string btSymsStr = "<NA>";
      if (btSize > 0) {
        btSymsStr = "";
        char ** btSyms = backtrace_symbols(allocAttrib.btAddrs, btSize);
        btSymsStr += btSyms ? btSyms[0] : 
          std::to_string((long)allocAttrib.btAddrs[0]);
        for (int i = 1; i < btSize; i++) {
          btSymsStr += std::string(" ") + (btSyms ? btSyms[i] : 
              std::to_string((long)allocAttrib.btAddrs[i])); 
        }
        allocAttrib.btSymsStr = btSymsStr;
      }
      allocAttrib.actualBtSize = btSize;
    }
    allocMap[allocPtr] = allocAttrib;
  }
  debug("rte_malloc_socket allocated:%p of type:%s, size:%u\n",
    allocPtr, type, size);
  return allocPtr;
}

extern "C" void
rte_free(void * addr) {
  debug("rte_free called for addr:%p\n", addr);
  if (debugTrace)
    PRINT_BACKTRACE;
  if (!dpdkLibHandle) {
    dpdkLibHandle = dlopen(dpdkLibName, RTLD_NOW);
    std::string errMsg = std::string("Failed to dlopen ") + dpdkLibName;
    verifyPerrorExit(dpdkLibHandle, errMsg);
  }
  if (!orig_rte_free) {
    orig_rte_free =
      (orig_rte_free_t)dlsym(dpdkLibHandle, "rte_free");
    std::string errMsg = "Failed to dlsym rte_free";
    verifyPerrorExit(orig_rte_free, errMsg.c_str());
  }
  if (addr && allocMap.find(addr) == allocMap.end()) {
    printf("Stray free OR a duplicate free of address: %p\n", addr);
    printf("Backtrace of free:\n");
    PRINT_BACKTRACE;
  }
  orig_rte_free(addr);
  if (addr) {
    allocMap.erase(addr);
  }
}

bool
sizeComp(std::pair< void *, allocAttrib_t > a,
       std::pair< void *, allocAttrib_t > b) {
  return a.second.size > b.second.size;
}

void
dumpAllocMapHandler(int signum) {
  std::vector< std::pair< void *, allocAttrib_t > > allocElems(allocMap.begin(),
    allocMap.end());
  std::sort(allocElems.begin(), allocElems.end(), sizeComp);
  char * dumpFileName = getenv("RTE_MALLOC_TRACK_DUMP_FILE");
  if (!dumpFileName) {
    dumpFileName = (char *)"/tmp/rteMallocTrackMapDump.txt";
  }
  FILE * dumpFile = fopen(dumpFileName, "w");
  std::string errMsg =
    std::string("fopen of ") + dumpFileName + std::string(" failed");
  verifyPerrorExit(dumpFile, errMsg.c_str());
  fprintf(dumpFile, "Addr,Size,Type,Backtrace\n");
  for (auto elem : allocElems) {
    fprintf(dumpFile,
            "%p,%u,%s,%s\n",
            elem.first,
            elem.second.size,
            elem.second.type,
            elem.second.btSymsStr.c_str());
  }
  fclose(dumpFile);
}

__attribute__((constructor)) void
init() {
  char * debugEnv = getenv("RTE_MALLOC_TRACK_DEBUG");
  if (debugEnv) {
    debugTrace = true;
  }
  char * btEnableEnv = getenv("RTE_MALLOC_TRACK_BACKTRACE");
  if (btEnableEnv) {
    btEnable = true;
  }
  char *mallocDpdkLibName = getenv("RTE_MALLOC_TRACK_DPDK_LIBNAME");
  if (mallocDpdkLibName) {
    dpdkLibName = mallocDpdkLibName;
  }
  struct sigaction new_action, old_action;
  new_action.sa_handler = dumpAllocMapHandler;
  sigemptyset(&new_action.sa_mask);
  new_action.sa_flags = SA_RESTART;
  sigaction(SIGRTMIN, NULL, &old_action);
  if (old_action.sa_handler != SIG_IGN)
    sigaction(SIGRTMIN, &new_action, NULL);
}
