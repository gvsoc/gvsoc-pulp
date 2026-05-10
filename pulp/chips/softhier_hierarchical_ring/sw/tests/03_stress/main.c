#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

// 32 KB per transfer to a single destination
#define TRANSFER_SIZE 32768
// Number of times to repeat the all-to-all broadcast
#define ITERATIONS 5

int main() {
  flex_global_barrier_init();

  uint32_t my_cid = flex_get_cluster_id();

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    printf("[Global Sync] Starting Maximum Traffic Stress Test...\n");
    printf("Pattern: All-to-All Broadcast (%d iterations)\n", ITERATIONS);
  }

  flex_global_barrier_polling();

  // ==========================================
  // THE CRUCIBLE: ALL-TO-ALL TRAFFIC
  // ==========================================
  // Every cluster queues up a DMA transfer to EVERY OTHER cluster
  // simultaneously before waiting.

  if (flex_is_dm_core()) {
    for (int iter = 0; iter < ITERATIONS; iter++) {

      // Queue up transfers to all other 7 clusters
      for (uint32_t target_cid = 0; target_cid < ARCH_NUM_CLUSTER;
           target_cid++) {
        if (target_cid != my_cid) {

          // Space out the memory offsets so they don't overwrite each other
          uint32_t local_offset = target_cid * TRANSFER_SIZE;
          uint32_t remote_offset = my_cid * TRANSFER_SIZE;

          flex_dma_async_1d(remote_cid(target_cid, remote_offset),
                            local(local_offset), TRANSFER_SIZE);
        }
      }

      // Wait for all 7 transfers to finish before starting the next wave
      flex_dma_async_wait_all();
    }
  }

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    printf("Maximum Traffic Stress Test Finished Successfully!\n");
  }

  flex_global_barrier_polling();
  flex_eoc_all(0);

  return 0;
}