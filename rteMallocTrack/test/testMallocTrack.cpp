/* Copyright © 2023 Arista Networks, Inc. All rights reserved.
 *
 * Use of this source code is governed by the MIT license that can be found
 * in the LICENSE file.
 */

#include <dpdk/rte_eal.h>
#include <dpdk/rte_lcore.h>
#include <dpdk/rte_mbuf.h>
#include <dpdk/rte_malloc.h>
#include <assert.h>
#include <signal.h>
#include <iostream>
#include <fstream>

const char *mallocTrackDumpFile = "/tmp/rteMallocTrackMapDump.txt";
void
input(const char *msg) {
  puts(msg);
  getchar();
  raise(SIGRTMIN);
  printf("Dumping contents of the %s file:\n\n", mallocTrackDumpFile);
  std::ifstream f(mallocTrackDumpFile);
  if (f.is_open())
    std::cout << f.rdbuf();
  puts("\nDone!\n");
}

int main(int argc, char **argv) {
  int ret = rte_eal_init(argc, argv);
  if (ret < 0)
    rte_panic("Cannot init EAL\n");
  input("rte_eal_init done. Press any key to malloc\n");

  void *p = rte_malloc("dummy", 1, 0);
  assert(p);
  input("rte_malloc done. Press any key to free\n");

  rte_free(p);
  input("rte_free done. Press any key to free again\n");

  rte_free(p);
  input("rte_free done. Press any key to do stray free\n");

  rte_free((void *)1);

  // Should have crashed by now and should not get here.
  printf("All done. Press any key to exit\n");

}
