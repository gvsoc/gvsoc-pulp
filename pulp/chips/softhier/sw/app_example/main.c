/*
#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

int main() {
  flex_global_barrier_init();
  for (int i = 0; i < 10; i++) {
    flex_global_barrier_polling();
    if (flex_get_cluster_id() == 0 && flex_is_first_core()) {
      printf("[Global Sync] iter %d\n", i);
    }
  }
  flex_global_barrier_polling();
  if (flex_get_cluster_id() == 0 && flex_is_dm_core()) {
    // DMA Read Data From Neighbor Cluster
    flex_dma_async_1d(local(0), remote_cid(1, 0), 1024);
    flex_dma_async_wait_all();
    printf("[DMA Pattern] Access Neighbor \n");
  }
  flex_global_barrier_polling();
  if (flex_is_dm_core()) {
    // DMA Row-wise Round Shift
    flex_dma_async_pattern_round_shift_right(0, 0, 1024);
    flex_dma_async_wait_all();
    if (flex_get_cluster_id() == 0)
      printf("[DMA Pattern] Round Shift \n");
  }
  flex_global_barrier_polling();
  if (flex_is_dm_core()) {
    // DMA Row-wise Round Shift
    flex_dma_async_pattern_dialog_to_dialog(0, 0, 1024);
    flex_dma_async_wait_all();
    if (flex_get_cluster_id() == 0)
      printf("[DMA Pattern] Dialog Transfer \n");
  }
  flex_global_barrier_polling();
  if (flex_get_cluster_id() == 0 && flex_is_first_core()) {
    printf("Chip: SoftHier\n");
    printf("Model: FlooNoc_Flex\n");
  }
  flex_global_barrier_polling();
  flex_eoc_all(0);
  return 0;
}
*/

#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

// Matrix Tile Size: 128 KB (32,768 floats)
#define TILE_SIZE 131072
// Number of GEMM Tile iterations
#define K_TILES 256

// TCDM Memory Layout (Fits perfectly within the 1MB TCDM limit)
#define A_BUF0 0x00000
#define A_BUF1 0x20000
#define B_BUF0 0x40000
#define B_BUF1 0x60000
#define C_BUF 0x80000

int main() {
  flex_global_barrier_init();

  uint32_t my_cid = flex_get_cluster_id();

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    printf("[Global Sync] Starting Systolic GEMM Benchmark...\n");
    printf("Pattern: Bi-directional Ring Shift (1D Cannon's Algorithm)\n");
    printf("Workload: Transferring 128 MB of system traffic...\n");
  }

  flex_global_barrier_polling();

  if (flex_get_cluster_id() == 0 && flex_is_first_core()) {
    flex_timer_start();
  }

  // ==========================================
  // SYSTOLIC GEMM KERNEL
  // ==========================================
  if (flex_is_dm_core()) {
    uint32_t a_offs[2] = {A_BUF0, A_BUF1};
    uint32_t b_offs[2] = {B_BUF0, B_BUF1};

    // 1D Systolic Ring topology coordinates
    uint32_t right_cid = (my_cid + 1) % ARCH_NUM_CLUSTER;
    uint32_t left_cid = (my_cid + ARCH_NUM_CLUSTER - 1) % ARCH_NUM_CLUSTER;

    int curr = 0;

    for (int k = 0; k < K_TILES; k++) {
      int next = 1 - curr;

      // 1. Initiate DMA: Shift Tile A to the Right neighbor
      flex_dma_async_1d(remote_cid(right_cid, a_offs[next]),
                        local(a_offs[curr]), TILE_SIZE);

      // 2. Initiate DMA: Shift Tile B to the Left neighbor
      flex_dma_async_1d(remote_cid(left_cid, b_offs[next]), local(b_offs[curr]),
                        TILE_SIZE);

      // 3. Simulate Compute Kernel (MACs)
      // This executes WHILE the DMA is transferring data across the NoC!
      volatile float *A = (float *)local(a_offs[curr]);
      volatile float *B = (float *)local(b_offs[curr]);
      volatile float *C = (float *)local(C_BUF);

      // A small unrolled loop to represent tile compute delay.
      // We keep it short so the benchmark remains NoC-bound, not Compute-bound.
      for (int i = 0; i < 256; i++) {
        C[i] += A[i] * B[i];
      }

      // 4. Wait for the DMA engine to finish shifting the 256 KB of tiles
      flex_dma_async_wait_all();

      // 5. Swap double buffers for the next iteration
      curr = next;
    }
  }

  flex_global_barrier_polling();

  if (flex_get_cluster_id() == 0 && flex_is_first_core()) {
    flex_timer_end();
  }

  if (my_cid == 0 && flex_is_first_core()) {
    printf("Systolic GEMM Benchmark Finished Successfully!\n");
  }

  // End of simulation
  flex_global_barrier_polling();
  flex_eoc_all(0);

  return 0;
}