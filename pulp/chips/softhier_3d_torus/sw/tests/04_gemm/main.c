#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

// Matrix Tile Size: 128 KB (32,768 floats)
#define TILE_SIZE 131072
// Number of GEMM Tile iterations
#define K_TILES 4096

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
    printf("[Global Sync] Starting Systolic GEMM Benchmark\n");
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
    // Topology-Aware 3D Hamiltonian Cycle (Gray Code Map)
    // Hardcoded via ALU logic to avoid .rodata memory fetches
    uint32_t right_cid, left_cid;

    switch (my_cid) {
    case 0:
      right_cid = 1;
      left_cid = 4;
      break;
    case 1:
      right_cid = 3;
      left_cid = 0;
      break;
    case 2:
      right_cid = 6;
      left_cid = 3;
      break;
    case 3:
      right_cid = 2;
      left_cid = 1;
      break;
    case 4:
      right_cid = 0;
      left_cid = 5;
      break;
    case 5:
      right_cid = 4;
      left_cid = 7;
      break;
    case 6:
      right_cid = 7;
      left_cid = 2;
      break;
    case 7:
      right_cid = 5;
      left_cid = 6;
      break;
    default:
      right_cid = 0;
      left_cid = 0;
      break;
    }

    int curr = 0;

    for (int k = 0; k < K_TILES; k++) {
      int next = 1 - curr;

      // ==========================================
      // PHASE 1: "Green Light" for Right Traffic
      // ==========================================
      flex_dma_async_1d(remote_cid(right_cid, a_offs[next]),
                        local(a_offs[curr]), TILE_SIZE);

      // Force the NoC to completely flush all Right-bound traffic
      // before allowing any Left-bound traffic to enter.
      flex_dma_async_wait_all();

      // ==========================================
      // PHASE 2: "Green Light" for Left Traffic
      // ==========================================
      flex_dma_async_1d(remote_cid(left_cid, b_offs[next]), local(b_offs[curr]),
                        TILE_SIZE);

      // CPU Compute happens in parallel with Phase 2
      volatile float *A = (float *)local(a_offs[curr]);
      volatile float *B = (float *)local(b_offs[curr]);
      volatile float *C = (float *)local(C_BUF);

      for (int i = 0; i < 256; i++) {
        C[i] += A[i] * B[i];
      }

      // Wait for the Left-bound traffic to finish
      flex_dma_async_wait_all();

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