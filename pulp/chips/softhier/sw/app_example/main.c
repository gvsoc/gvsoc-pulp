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

*/
/*
#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

// Transfer parameters
#define TILE_SIZE 16384    // 16 KB per transfer
#define NUM_ITERATIONS 256 // Number of outer loops
#define BATCH_SIZE                                                             \
  8 // Async DMA requests before syncing (Must be <= ARCH_IDMA_OUTSTAND_TXN)

// Memory Layout (Fits within 1MB TCDM limit)
#define SRC_BUF 0x00000 // Outgoing data buffer
#define DST_BUF 0x80000 // Incoming data buffer base address

// Lightweight Xorshift32 Pseudo-Random Number Generator for bare-metal
uint32_t prng_state;

uint32_t get_random_cid() {
  prng_state ^= prng_state << 13;
  prng_state ^= prng_state >> 17;
  prng_state ^= prng_state << 5;
  // Map the random number to a valid cluster ID (0 to ARCH_NUM_CLUSTER - 1)
  return prng_state % ARCH_NUM_CLUSTER;
}

int main() {
  // 1. Initialize Global Barrier
  flex_global_barrier_init();

  uint32_t my_cid = flex_get_cluster_id();

  // Seed the PRNG uniquely for each cluster to ensure diverse traffic
  // distribution. XOR with a magic number to ensure the state is never exactly
  // 0.
  prng_state = 0xDEADBEEF ^ (my_cid + 1);

  flex_global_barrier_polling();

  // 2. Master Core Prints Configuration
  if (my_cid == 0 && flex_is_first_core()) {
    printf("[Global Sync] Starting Uniform Random NoC Benchmark...\n");
    printf("Topology: Flex-2DMesh (%dx%d)\n", ARCH_NUM_CLUSTER_X,
           ARCH_NUM_CLUSTER_Y);
    printf("Traffic: Uniform Random\n");
    printf("Workload: %d KB per DMA, %d batch size, %d iterations\n",
           (TILE_SIZE / 1024), BATCH_SIZE, NUM_ITERATIONS);
  }

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    flex_timer_start();
  }

  // ==========================================
  // UNIFORM RANDOM TRAFFIC KERNEL
  // ==========================================
  if (flex_is_dm_core()) {

    for (int k = 0; k < NUM_ITERATIONS; k++) {

      // Fire a batch of asynchronous DMAs to saturate the NoC injection ports
      for (int b = 0; b < BATCH_SIZE; b++) {
        // Pick a random destination cluster in the 3D Mesh
        uint32_t dest_cid = get_random_cid();

        // Offset the destination buffer slightly per batch to distribute
        // writes across different TCDM memory banks on the remote node
        uint32_t dst_offset = DST_BUF + (b * TILE_SIZE);

        // Initiate NoC transfer
        flex_dma_async_1d(remote_cid(dest_cid, dst_offset), local(SRC_BUF),
                          TILE_SIZE);
      }

      // Wait for the entire batch to successfully traverse the NoC and write to
      // memory
      flex_dma_async_wait_all();
    }
  }

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    flex_timer_end();
    printf("Uniform Random Benchmark Finished Successfully!\n");
  }

  // End of simulation
  flex_global_barrier_polling();
  flex_eoc_all(0);

  return 0;
}
*/

#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

#define NUM_TRIALS 1000
#define SRC_BUF 0x00000
#define DST_BUF 0x80000
#define SHARED_BUF 0x40000

uint32_t prng_state;

uint32_t get_random_cid() {
  prng_state ^= prng_state << 13;
  prng_state ^= prng_state >> 17;
  prng_state ^= prng_state << 5;
  return prng_state % ARCH_NUM_CLUSTER;
}

static inline uint32_t get_cycles() {
  uint32_t cycles;
  asm volatile("csrr %0, mcycle" : "=r"(cycles));
  return cycles;
}

int main() {
  flex_global_barrier_init();

  uint32_t my_cid = flex_get_cluster_id();

  // Initialize shared memory on cluster 0
  if (my_cid == 0 && flex_is_first_core()) {
    volatile uint32_t *shared_latencies = (volatile uint32_t *)local(SHARED_BUF);
    for (int i = 0; i < NUM_TRIALS; i++) {
      shared_latencies[i] = 0;
    }
  }

  flex_global_barrier_polling();

  prng_state = 0xDEADBEEF;

  for (int i = 0; i < NUM_TRIALS; i++) {
    uint32_t src_cid = get_random_cid();
    uint32_t dst_cid;
    do {
      dst_cid = get_random_cid();
    } while (dst_cid == src_cid);

    // Sync all clusters so the network is quiet before the transfer starts
    flex_global_barrier_polling();

    if (my_cid == src_cid && flex_is_dm_core()) {
      uint64_t dst_addr = remote_cid(dst_cid, DST_BUF);
      uint64_t src_addr = local(SRC_BUF);
      size_t transfer_size = 64; // 512 bits (1 flit)

      // Minimal timing overhead setup:
      // Configure local registers with arguments
      register uint32_t reg_dst_low asm("a0") = dst_addr >> 0;
      register uint32_t reg_dst_high asm("a1") = dst_addr >> 32;
      register uint32_t reg_src_low asm("a2") = src_addr >> 0;
      register uint32_t reg_src_high asm("a3") = src_addr >> 32;
      register uint32_t reg_size asm("a4") = transfer_size;

      // Set DMA source (outside the cycle-counter timing window)
      asm volatile(".word %0\n" ::"i"(R_TYPE_ENCODE(DMSRC_FUNCT7, 13, 12,
                                                    XDMA_FUNCT3, 0, OP_CUSTOM1)),
                   "r"(reg_src_high), "r"(reg_src_low));

      // Set DMA destination (outside the cycle-counter timing window)
      asm volatile(".word %0\n" ::"i"(R_TYPE_ENCODE(DMDST_FUNCT7, 11, 10,
                                                    XDMA_FUNCT3, 0, OP_CUSTOM1)),
                   "r"(reg_dst_high), "r"(reg_dst_low));

      // Compiler memory barrier to prevent reordering of instructions
      asm volatile("" ::: "memory");

      // Start the timer
      uint32_t start = get_cycles();

      // Trigger the DMA copy (dmcpyi)
      register uint32_t reg_txid asm("a0");
      asm volatile(".word %1\n"
                   : "=r"(reg_txid)
                   : "i"(R_TYPE_ENCODE(DMCPYI_FUNCT7, 0b00000, 14, XDMA_FUNCT3,
                                       10, OP_CUSTOM1)),
                     "r"(reg_size));

      // Wait for complete
      bare_dma_wait_all();

      // Stop the timer
      uint32_t end = get_cycles();

      // Compiler memory barrier
      asm volatile("" ::: "memory");

      uint32_t latency = end - start;

      // Write measured latency into cluster 0's shared buffer
      volatile uint32_t *ptr = (volatile uint32_t *)remote_cid(0, SHARED_BUF + i * 4);
      *ptr = latency;
    }

    // Sync all clusters again to ensure the active transfer has fully finished
    // and the NoC is completely quiet before the next iteration
    flex_global_barrier_polling();
  }

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    volatile uint32_t *shared_latencies = (volatile uint32_t *)local(SHARED_BUF);
    uint32_t sum = 0;
    uint32_t min_lat = 0xFFFFFFFF;
    uint32_t max_lat = 0;
    uint32_t count = 0;

    prng_state = 0xDEADBEEF;

    printf("\n--- Zero-Load Latency Benchmark Results ---\n");
    for (int i = 0; i < NUM_TRIALS; i++) {
      uint32_t src_cid = get_random_cid();
      uint32_t dst_cid;
      do {
        dst_cid = get_random_cid();
      } while (dst_cid == src_cid);

      uint32_t latency = shared_latencies[i];
      sum += latency;
      count++;
      if (latency < min_lat) min_lat = latency;
      if (latency > max_lat) max_lat = latency;

      printf("  Trial %3d: Cluster %2d -> Cluster %2d | Latency: %d cycles\n",
             i, src_cid, dst_cid, latency);
    }

    if (count > 0) {
      uint32_t avg_int = sum / count;
      uint32_t avg_frac = ((sum * 100) / count) % 100;
      printf("\nSummary:\n");
      printf("  Average Latency: %d.%02d cycles\n", avg_int, avg_frac);
      printf("  Lowest Latency:  %d cycles\n", min_lat);
      printf("  Highest Latency: %d cycles\n", max_lat);
    }
    printf("-------------------------------------------\n");
  }

  flex_global_barrier_polling();
  flex_eoc_all(0);

  return 0;
}