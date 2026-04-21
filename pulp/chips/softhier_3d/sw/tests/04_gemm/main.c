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

    // Topology-Aware 3D Hamiltonian Cycle (Gray Code Map)
    // Path: C0 -> C1 -> C3 -> C2 -> C6 -> C7 -> C5 -> C4 -> C0
    // Every shift is exactly 1 physical hop. No diagonal cross-routing!
    const uint32_t right_neighbor[8] = {1, 3, 6, 2, 0, 4, 7, 5};
    const uint32_t left_neighbor[8] = {4, 0, 3, 1, 5, 7, 2, 6};

    uint32_t right_cid = right_neighbor[my_cid];
    uint32_t left_cid = left_neighbor[my_cid];

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