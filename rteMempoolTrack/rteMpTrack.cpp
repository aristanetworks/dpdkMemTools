/* Copyright © 2023 Arista Networks, Inc. All rights reserved.
 *
 * Use of this source code is governed by the MIT license that can be found
 * in the LICENSE file.
 */

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <unordered_map>
#include <assert.h>
#include <string.h>
#include <string>
#include <dpdk/rte_mempool.h>
#include <vector>
#define MAX_ERR_MSG 2048
#define MAX_BT_FRAMES 16
bool debugTrace = false; // Flip to en/disable debug prints
char *dpdkLibName;
void *dpdkLibHandle;
uint16_t numBtFrames = 1; // Set to 0 to disable Backtrace capture
char *mpToTrack = (char *)"(nil)";

// Helper to print formatted debug messages if enabled
void
debug(const char * format, ...) {
  if (debugTrace) {
    va_list argList;
    va_start(argList, format);
    vfprintf(stdout, format, argList);
    va_end(argList);
  }
}

// Helper to print formatted fatal messages and die
void
fatal(const char * format, ...) {
  va_list argList;
  va_start(argList, format);
  vfprintf(stderr, format, argList);
  va_end(argList);
  assert(0);
}

// Helper to verify the condition and exit if it is false
inline void
verifyPerrorExit(bool condition, const char* errMsg) {
  if (!condition) {
    perror(errMsg);
    exit(1);
  }
}

// mempool track data to capture alloc and free backtrace
struct mpTrackData {
  void *allocBt[MAX_BT_FRAMES];
  void *freeBt[MAX_BT_FRAMES];
  uint8_t allocBtLen;
  uint8_t freeBtLen;
};
std::unordered_map< void *, struct mpTrackData *> mpTrackMap;
typedef std::unordered_map< void *, struct mpTrackData *>::iterator
   mpTrackMapIter_t;

int orig_ops_index;
int mpTrackOpsIdx;

typedef int
(*rte_mempool_set_ops_byname_t)(struct rte_mempool *mp, const char *name,
   void *pool_config);
rte_mempool_set_ops_byname_t orig_rte_mempool_set_ops_byname;

// Override the mempool ops to intercept the alloc and free
int
rte_mempool_set_ops_byname(struct rte_mempool *mp, const char *name,
   void *pool_config) {
  verifyPerrorExit(dpdkLibName,
   "dpdkLibName is NULL, set it using environ RTE_MP_TRACK_DPDK_LIBNAME");

  // Backup the original mempool ops
  if (!dpdkLibHandle) {
    dpdkLibHandle = dlopen(dpdkLibName, RTLD_NOW);
    char errMsg[MAX_ERR_MSG];
    snprintf(errMsg, sizeof( errMsg), "Failed to dlopen %s", dpdkLibName );
    verifyPerrorExit(dpdkLibHandle, errMsg);
  }
  if (!orig_rte_mempool_set_ops_byname) {
    orig_rte_mempool_set_ops_byname =
      (rte_mempool_set_ops_byname_t)dlsym(dpdkLibHandle,
      "rte_mempool_set_ops_byname");
    char *errMsg = (char *)"Failed to dlsym rte_mempool_set_ops_byname";
    verifyPerrorExit(orig_rte_mempool_set_ops_byname, errMsg);
  }

  // Replace the mempool ops
  int ret = orig_rte_mempool_set_ops_byname(mp, name, pool_config);
  if (ret || !mp || !mpToTrack || strcmp(mp->name, mpToTrack)) {
    debug("Skipping non interested mp:%p, mempool:%s, mpToTrack:%s, ret:%d\n",
      mp, mp ? mp->name : "(nil)", mpToTrack, ret);
  } else {
    orig_ops_index = mp->ops_index;
    // Ideally we need to do it by interposing rte_mempool_create
    mp->cache_size = 0;
    mp->ops_index = mpTrackOpsIdx;
  }
  return ret;
}

// Track the mempool free and detect invalid frees
static int
mpTrack_mp_enqueue(struct rte_mempool *mp, void * const *obj_table,
  unsigned n) {
  for (int i = 0; i < n; i++) {
    void *obj = obj_table[ i ];
    struct mpTrackData *mpD = NULL;
    mpTrackMapIter_t iter = mpTrackMap.find(obj);
    // Detect stray and double free
    if (iter != mpTrackMap.end()) {
      if (numBtFrames > 0) {
        mpD = iter->second;
        if (!mpD) {
          fatal("mp:%s Stray free of %p is not expected\n",
            mp->name, obj);
        }

        if (mpD->freeBtLen) {
          std::string btSymStr = "";
          char **btSyms = backtrace_symbols(mpD->freeBt, mpD->freeBtLen);
          if (btSyms) {
            for (int i = 0; i < mpD->freeBtLen; i++) {
              btSymStr += btSyms[ i ];
              btSymStr += "\n";
            }
            free(btSyms);
          }
          fatal("mp:%s Duplicate free of %p is not expected."
                "Already freed at:%s\n",
            mp->name, obj, btSymStr.c_str());
        }
        int btSize = backtrace(mpD->freeBt, numBtFrames);
        if (btSize > 0) {
          mpD->freeBtLen = btSize;   
        }
      }
      mpD->allocBtLen = 0;
    } else {
      // Track the free
      struct mpTrackData *mpD = NULL;
      if (numBtFrames > 0) {
        mpD = new struct mpTrackData;
        mpD->allocBtLen = 0;
        int btSize = backtrace(mpD->freeBt, numBtFrames);
        if (btSize > 0) {
          mpD->freeBtLen = btSize;
        }
      }
      mpTrackMap[obj] = mpD;
    }
  }

  // Pass the baton to the orig enqueue function
  struct rte_mempool_ops * ops = rte_mempool_get_ops(orig_ops_index);
  return ops->enqueue(mp, obj_table, n);
}

// Track the mempool alloc
static int
mpTrack_mc_dequeue(struct rte_mempool *mp, void **obj_table, unsigned n)
{
  struct rte_mempool_ops * ops = rte_mempool_get_ops(orig_ops_index);
  // Get the objs allocated using the original memops
  int ret = ops->dequeue(mp, obj_table, n);
  for (int i = 0; i < n; i++) {
    void *obj = obj_table[ i ];
    struct mpTrackData *mpD = NULL;
    mpTrackMapIter_t iter = mpTrackMap.find(obj);
    // Detect a stray alloc. The mempool elements need to be
    // enqueued( freed ) before getting dequeued( alloc )
    if (iter != mpTrackMap.end()) {
      if (numBtFrames > 0) {
        mpD = iter->second;
        if (!mpD) {
          fatal("mp:%s Stray alloc of %p is not expected\n", mp->name, obj);
        }
        // Track the alloc
        mpD->freeBtLen = 0;
        int btSize = backtrace(mpD->allocBt, numBtFrames);
        if (btSize > 0) {
          mpD->allocBtLen = btSize;
        }
      }
    } else {
      fatal("mp:%s Stray alloc of %p is not expected. ret:%d\n",
        mp->name, obj, ret);
    }
  }
  return ret;
}

// Nothing much to do, just pass the baton
static int
mpTrack_alloc(struct rte_mempool *mp)
{
  struct rte_mempool_ops * ops = rte_mempool_get_ops(orig_ops_index);
  return ops->alloc(mp);
}

// Nothing much to do, just pass the baton
static void
mpTrack_free(struct rte_mempool *mp)
{
  struct rte_mempool_ops * ops = rte_mempool_get_ops(orig_ops_index);
  return ops->free(mp);
}

// Nothing much to do, just pass the baton
static unsigned
mpTrack_get_count(const struct rte_mempool *mp)
{
  struct rte_mempool_ops *ops = rte_mempool_get_ops(orig_ops_index);
  return ops->get_count(mp);
}

// Helper to get the backtrace symbols as space seperated strings
std::string
getBtString(void **btBuffer, int btSize) {
  std::string btSymsStr = "";
  char ** btSyms = backtrace_symbols(btBuffer, btSize);
  btSymsStr += btSyms ? btSyms[0] : 
  std::to_string(( long)btBuffer[0] );
  for (int i = 1; i < btSize; i++) {
    btSymsStr += std::string(" ") + ( btSyms ? btSyms[ i ] : 
      std::to_string(( long)btBuffer[ i ] ) ); 
  }
  return btSymsStr;
}


// Helper to dump the mpTrackMap into the configured dumpFileName
void
dumpAllocMapHandler(int signum) {
  std::vector<std::pair<void*, struct mpTrackData*>> allocElems(mpTrackMap.begin(),
    mpTrackMap.end());
  char *dumpFileName = getenv("RTE_MP_TRACK_DUMP_FILE");
  if (!dumpFileName) {
    dumpFileName = (char *)"/tmp/mpTrackMapDump.txt";
  }
  FILE * dumpFile = fopen(dumpFileName, "w");
  std::string errMsg =
    std::string("fopen of ") + dumpFileName + std::string( " failed" );
  verifyPerrorExit(dumpFile, errMsg.c_str());

  // Start the dump
  fprintf(dumpFile, "Addr,Alloc-Backtrace,Free-Backtrace\n");
  for (auto elem : allocElems) {
    struct mpTrackData *mpD = elem.second;
    std::string allocBt = "<NA>";
    std::string freeBt = "<NA>";
    // Dump only the outstanding allocations unless RTE_MP_TRACK_DEBUG
    // Dumping the whole mempool elements can be overwhelming otherwise
    if ((!mpD->allocBtLen || mpD->freeBtLen) && !debugTrace)
      continue;
    if (mpD && mpD->allocBtLen)
      allocBt = getBtString(mpD->allocBt, mpD->allocBtLen);
    if (mpD && mpD->freeBtLen)
      freeBt = getBtString(mpD->freeBt, mpD->freeBtLen);
    fprintf(dumpFile, "%p,%s,%s\n",
      elem.first, allocBt.c_str(), freeBt.c_str());
  }
  fclose(dumpFile);
}

// Library main entry point
__attribute__((constructor)) void
init() {
  char *debugEnv = getenv("RTE_MP_TRACK_DEBUG");
  if (debugEnv) {
    debugTrace = true;
  }
  char *numBtEnv = getenv("RTE_MP_TRACK_NUM_BT");
  if (numBtEnv) {
    numBtFrames = atoi(numBtEnv);
  }
  char *mpName = getenv("RTE_MP_TRACK_NAME");
  if (mpName) {
    mpToTrack = mpName;
  }
  char *mpDpdkLibName = getenv("RTE_MP_TRACK_DPDK_LIBNAME");
  if (mpDpdkLibName) {
    dpdkLibName = mpDpdkLibName;
  }

  /* ops for mempool alloc/free tracker */
  static const struct rte_mempool_ops ops_mp_alloc_track = {
    .name = "mpAllocTrack",
    .alloc = mpTrack_alloc,
    .free = mpTrack_free,
    .enqueue = mpTrack_mp_enqueue,
    .dequeue = mpTrack_mc_dequeue,
    .get_count = mpTrack_get_count,
  };
  mpTrackOpsIdx = rte_mempool_register_ops(&ops_mp_alloc_track);

  // Register the SIGRTMIN handler used to dump the mpTrackMap
  struct sigaction new_action, old_action;
  new_action.sa_handler = dumpAllocMapHandler;
  sigemptyset(&new_action.sa_mask);
  new_action.sa_flags = SA_RESTART;
  sigaction(SIGRTMIN, NULL, &old_action);
  if (old_action.sa_handler != SIG_IGN)
    sigaction(SIGRTMIN, &new_action, NULL);
}

