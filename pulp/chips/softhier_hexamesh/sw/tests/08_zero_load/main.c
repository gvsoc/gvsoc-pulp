#include "softhier_arch.h"
#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

// 64 Bytes = Exactly 1 Flit (512 bits).
// Keeps serialization delay at an absolute minimum.
#define TILE_SIZE 64

// Read the RISC-V hardware cycle counter
static inline uint32_t get_cycles() {
  uint32_t cycles;
  asm volatile("csrr %0, mcycle" : "=r"(cycles));
  return cycles;
}

int main() {
  flex_global_barrier_init();

  uint32_t my_cid = flex_get_cluster_id();

  // Synchronize all 127 clusters to ensure the network is fully awake and empty
  flex_global_barrier_polling();

  // ONLY Cluster 0 will inject traffic. All other clusters sit completely idle.
  if (my_cid == 0 && flex_is_dm_core()) {
    printf("\n--- Zero-Load Latency Benchmark ---\n");

    uint32_t total_rtt_cycles = 0;
    uint32_t valid_destinations = 0;

    // Ping every other cluster on the chip (Cluster 1 through 126)
    for (uint32_t dest = 1; dest < ARCH_NUM_CLUSTER; dest++) {

      // Start the timer
      uint32_t start_time = get_cycles();

      // Inject exactly 1 packet
      flex_dma_async_1d(remote_cid(dest, 0x80000), local(0x00000), TILE_SIZE);

      // CRITICAL: Force the CPU to sleep until the network acknowledgment
      // returns. This guarantees the NoC is 100% empty before the loop
      // continues.
      flex_dma_async_wait_all();

      // Stop the timer
      uint32_t end_time = get_cycles();

      uint32_t rtt = end_time - start_time;
      total_rtt_cycles += rtt;
      valid_destinations++;
    }

    uint32_t avg_rtt = total_rtt_cycles / valid_destinations;

    printf("Target Nodes Pinged: %u\n", valid_destinations);
    printf("Average RTT (End-to-End): %u cycles\n", avg_rtt);

    // One-way latency is roughly half the Round Trip Time (Request -> Target,
    // Response <- Target)
    printf("Estimated 1-Way Latency: %u cycles\n\n", avg_rtt / 2);
  }

  // Synchronize before exiting
  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    printf("Benchmark Finished Successfully!\n");
  }

  flex_eoc_all(0);
  return 0;
}