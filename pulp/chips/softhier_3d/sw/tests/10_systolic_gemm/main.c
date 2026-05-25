#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"
#include "softhier_arch.h"

// ==========================================
// Benchmark Configuration (modifiable by Python)
// ==========================================
#define TILE_DIM 64
#define K_TILES 16

#define TILE_BYTES (TILE_DIM * TILE_DIM * sizeof(float))

// TCDM Memory Layout
#define A_BUF0 0x00000
#define A_BUF1 0x10000
#define B_BUF0 0x20000
#define B_BUF1 0x30000
#define C_BUF  0x40000
#define MAIN_A 0x50000
#define MAIN_B 0x60000

// Helper to get cluster ID from 3D coordinates
static inline uint32_t get_cid_3d(uint32_t x, uint32_t y, uint32_t z) {
  return z * (ARCH_NUM_CLUSTER_X * ARCH_NUM_CLUSTER_Y) + y * ARCH_NUM_CLUSTER_X + x;
}

// Compute Tile GEMM
void compute_gemm_multicore(uint32_t c_off, uint32_t a_off, uint32_t b_off) {
  // If the architecture has dedicated compute cores, the DM core skips math
  if (ARCH_NUM_CORE_PER_CLUSTER > 1 && flex_is_dm_core()) {
      return; 
  }

  float *C = (float *)local(c_off);
  float *A = (float *)local(a_off);
  float *B = (float *)local(b_off);

  // Determine compute division
  uint32_t num_compute = (ARCH_NUM_CORE_PER_CLUSTER > 1) ? (ARCH_NUM_CORE_PER_CLUSTER - 1) : 1;
  uint32_t compute_id = flex_get_core_id();

  // Chunk the outer loop 'i' across available compute cores
  uint32_t chunk_size = TILE_DIM / num_compute;
  uint32_t start_i = compute_id * chunk_size;
  // Ensure the last core picks up any remainder
  uint32_t end_i = (compute_id == num_compute - 1) ? TILE_DIM : start_i + chunk_size;

  for (uint32_t i = start_i; i < end_i; i++) {
    for (uint32_t k = 0; k < TILE_DIM; k++) {
      float a_val = A[i * TILE_DIM + k];
      for (uint32_t j = 0; j < TILE_DIM; j++) {
        C[i * TILE_DIM + j] += a_val * B[k * TILE_DIM + j];
      }
    }
  }
}

// Read the RISC-V hardware cycle counter
static inline uint32_t get_cycles() {
  uint32_t cycles;
  asm volatile("csrr %0, mcycle" : "=r"(cycles));
  return cycles;
}

int main() {
  flex_global_barrier_init();

  uint32_t my_cid = flex_get_cluster_id();
  uint32_t X = my_cid % ARCH_NUM_CLUSTER_X;
  uint32_t Y = (my_cid / ARCH_NUM_CLUSTER_X) % ARCH_NUM_CLUSTER_Y;
  uint32_t Z = my_cid / (ARCH_NUM_CLUSTER_X * ARCH_NUM_CLUSTER_Y);

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    printf("[Global Sync] Starting 3D Systolic Wavefront GEMM...\n");
    printf("Topology: 3D Mesh (%dx%dx%d)\n", ARCH_NUM_CLUSTER_X, ARCH_NUM_CLUSTER_Y, ARCH_NUM_CLUSTER_Z);
    printf("Strategy: Z-axis handles independent batches\n");
    printf("Tile Dimension: %dx%d floats (%d Bytes)\n", TILE_DIM, TILE_DIM, TILE_BYTES);
    printf("K-Tiles: %d\n", K_TILES);
  }

  // Initialize C_BUF to 0
  if (flex_is_dm_core()) {
    float *C = (float *)local(C_BUF);
    for (int i = 0; i < TILE_DIM * TILE_DIM; i++) {
      C[i] = 0.0f;
    }
  }

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    flex_timer_start();
  }

  uint32_t start_cycles = get_cycles();

  uint32_t a_offs[2] = {A_BUF0, A_BUF1};
  uint32_t b_offs[2] = {B_BUF0, B_BUF1};
  
  int curr = 0;
  int next = 1;
  
  uint32_t total_steps = K_TILES + ARCH_NUM_CLUSTER_X + ARCH_NUM_CLUSTER_Y - 2;

  // ==========================================
  // PHASE 1: Prime the Pipeline (t = -1)
  // ==========================================
  if (flex_is_dm_core()) {
    if (X == 0) flex_dma_async_1d(local(a_offs[curr]), local(MAIN_A), TILE_BYTES);
    if (Y == 0) flex_dma_async_1d(local(b_offs[curr]), local(MAIN_B), TILE_BYTES);
    flex_dma_async_wait_all(); // Wait for prime to finish
  }
  
  // ALL cores must wait for the prime to finish so buffers are ready
  flex_global_barrier_polling();

  // ==========================================
  // PHASE 2: Main Systolic Loop
  // ==========================================
  for (uint32_t t = 0; t <= total_steps; t++) {
    
    bool active_curr = (t >= X + Y) && (t < X + Y + K_TILES);
    // Boundary fix: Fetch logic ensures we don't pull past K_TILES
    bool active_next = (t + 1 >= X + Y) && (t + 1 < X + Y + K_TILES);

    // 1. DMA Operations (Handled strictly by the DM engine)
    if (flex_is_dm_core()) {
      
      // A. Fetch NEXT tile from Memory (Edge nodes only)
      if (active_next) {
        if (X == 0) flex_dma_async_1d(local(a_offs[next]), local(MAIN_A), TILE_BYTES);
        if (Y == 0) flex_dma_async_1d(local(b_offs[next]), local(MAIN_B), TILE_BYTES);
      }

      // B. Push CURRENT tile to neighbors (Non-edge nodes)
      if (active_curr) {
        if (X < ARCH_NUM_CLUSTER_X - 1) {
          uint32_t east_cid = get_cid_3d(X + 1, Y, Z);
          flex_dma_async_1d(remote_cid(east_cid, a_offs[next]), local(a_offs[curr]), TILE_BYTES);
        }
        if (Y < ARCH_NUM_CLUSTER_Y - 1) {
          uint32_t south_cid = get_cid_3d(X, Y + 1, Z);
          flex_dma_async_1d(remote_cid(south_cid, b_offs[next]), local(b_offs[curr]), TILE_BYTES);
        }
      }
    }

    // 2. Compute Operations (Handled by compute cores)
    if (active_curr) {
      compute_gemm_multicore(C_BUF, a_offs[curr], b_offs[curr]);
    }

    // 3. Synchronization
    if (flex_is_dm_core()) {
      flex_dma_async_wait_all(); // DM core ensures its transfers are done
    }
    
    // CRITICAL: Global barrier placed OUTSIDE the DM if-statement.
    // This syncs the DM core with the compute cores via intra-cluster sync, 
    // ensuring no buffer swaps happen while math is still running.
    flex_global_barrier_polling();

    // 4. Swap buffers for double-buffering
    curr = next;
    next = 1 - curr;
  }

  flex_global_barrier_polling();
  uint32_t end_cycles = get_cycles();

  if (my_cid == 0 && flex_is_first_core()) {
    flex_timer_end();
    uint32_t total_cycles = end_cycles - start_cycles;
    
    // Total floating point operations (MAC = 2 ops)
    // Global Matrix M = ARCH_NUM_CLUSTER_Y * TILE_DIM
    // Global Matrix N = ARCH_NUM_CLUSTER_X * TILE_DIM
    // Global Matrix K = K_TILES * TILE_DIM
    // Since Z handles independent batches, total ops are multiplied by Z.
    // Total Ops = 2 * (Y_clusters * X_clusters * Z_clusters) * (TILE_DIM^3) * K_TILES
    //           = 2 * ARCH_NUM_CLUSTER * K_TILES * TILE_DIM^3
    
    uint64_t total_ops = 2ULL * ARCH_NUM_CLUSTER * K_TILES * TILE_DIM * TILE_DIM * TILE_DIM;
    
    // GFLOPS calculation assuming 1GHz clock (1 cycle = 1 ns)
    // ops / cycles = GFLOPS at 1GHz
    uint32_t ops_per_cycle_int = total_ops / total_cycles;
    uint32_t ops_per_cycle_frac = ((total_ops * 100) / total_cycles) % 100;

    printf("C-Total Cycles: %u\n", total_cycles);
    printf("C-Ops/Cycle: %u.%02u\n", ops_per_cycle_int, ops_per_cycle_frac);
    printf("Systolic GEMM Benchmark Finished Successfully!\n");
  }

  flex_global_barrier_polling();
  flex_eoc_all(0);

  return 0;
}
