#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

// Define a heavy transfer size (16 KB per transfer)
#define TRANSFER_SIZE 16384

int main() {
  // Initialize the global hardware barrier
  flex_global_barrier_init();

  uint32_t my_cid = flex_get_cluster_id();

  // Synchronize all clusters before starting the benchmark
  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    printf("[Global Sync] Starting Global Traffic Stress Test...\n");
  }

  flex_global_barrier_polling();

  // ==========================================
  // PHASE 1: INCAST (All-to-One)
  // ==========================================
  // All secondary clusters write into Cluster 0 simultaneously.
  // This will heavily saturate Router 0's queues and test backpressure.

  if (flex_is_dm_core()) {
    if (my_cid != 0) {
      // Write to a unique offset in Cluster 0's memory to avoid overwriting
      uint32_t dest_offset = my_cid * TRANSFER_SIZE;
      flex_dma_async_1d(remote_cid(0, dest_offset), local(0), TRANSFER_SIZE);
    }
    flex_dma_async_wait_all(); // Wait for DMA to finish the heavy lifting
  }

  // Wait for all data to settle
  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    printf("[Phase 1] Incast (All-to-0) Complete.\n");
  }

  flex_global_barrier_polling();

  // ==========================================
  // PHASE 2: CROSS-MESH TRAFFIC
  // ==========================================
  // Every cluster sends data to its diametric opposite (e.g., 0->4, 1->5).
  // This maximizes hop counts and forces packets to cross paths in the routers.

  if (flex_is_dm_core()) {
    // Calculate the cluster furthest away (Assuming 8 clusters total)
    uint32_t target_cid = (my_cid + (ARCH_NUM_CLUSTER / 2)) % ARCH_NUM_CLUSTER;

    flex_dma_async_1d(remote_cid(target_cid, 0), local(TRANSFER_SIZE),
                      TRANSFER_SIZE);
    flex_dma_async_wait_all();
  }

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    printf("[Phase 2] Cross-Mesh Traffic Complete.\n");
    printf("Global Traffic Test Finished Successfully!\n");
  }

  // End of simulation
  flex_global_barrier_polling();
  flex_eoc_all(0);

  return 0;
}