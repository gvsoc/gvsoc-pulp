#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

// Transfer parameters
#define TILE_SIZE 16384     // 16 KB per transfer
#define NUM_ITERATIONS 1024 // Number of outer loops
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
    printf("Topology: 3DMesh (%dx%dx%d)\n", ARCH_NUM_CLUSTER_X,
           ARCH_NUM_CLUSTER_Y, ARCH_NUM_CLUSTER_Z);
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