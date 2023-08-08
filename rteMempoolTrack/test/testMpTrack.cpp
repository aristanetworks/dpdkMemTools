/* Copyright © 2023 Arista Networks, Inc. All rights reserved.
 *
 * Use of this source code is governed by the MIT license that can be found
 * in the LICENSE file.
 */

#include <dpdk/rte_eal.h>
#include <dpdk/rte_lcore.h>
#include <dpdk/rte_mbuf.h>
#include <assert.h>
#include <signal.h>
#include <iostream>
#include <fstream>

const char *mpTrackDumpFile = "/tmp/mpTrackMapDump.txt";
void
input(const char *msg) {
  puts(msg);
  getchar();
  raise(SIGRTMIN);
  printf("Dumping contents of the %s file:\n\n", mpTrackDumpFile);
  std::ifstream f(mpTrackDumpFile);
  if (f.is_open())
    std::cout << f.rdbuf();
  puts("\nDone!\n");
}

int main(int argc, char **argv) {
  int ret = rte_eal_init(argc, argv);
  if (ret < 0)
    rte_panic("Cannot init EAL\n");
  input("rte_eal_init done. Press any key to create the mempool\n");

  rte_mempool *mp = rte_pktmbuf_pool_create("mpTrackPool", 2, 1, 0, 0,
    rte_lcore_id());
  assert(mp);
  input("rte_pktmbuf_pool_create done. Press any key to alloc\n");

  rte_mbuf *pkt = rte_pktmbuf_alloc(mp);
  assert(pkt);
  input("rte_pktmbuf_alloc done. Press any key to alloc again\n");

  pkt = rte_pktmbuf_alloc(mp);
  assert(pkt);
  input("rte_pktmbuf_alloc done. Press any key to free\n");

  rte_pktmbuf_free(pkt);
  input("rte_pktmbuf_free done. Press any key to free again\n");

  rte_pktmbuf_free(pkt);
  // Should have crashed by now and should not get here.
  printf("All done. Press any key to exit\n");
}
