#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

// Helper function to read the cycle counter
inline uint32_t read_cycle() {
  uint32_t cycles;
  asm volatile("csrr %0, mcycle" : "=r"(cycles));
  return cycles;
}

int main() {
  // 1. Initialize global barrier
  flex_global_barrier_init();

  // Large transfer size (e.g., 64KB per cluster)
  size_t transfer_size = 65536;
  uint32_t local_offset = 0x1000;
  uint32_t remote_offset = 0x2000;

  // 2. Synchronize before starting the timer
  flex_global_barrier_polling();

  uint32_t start_cycles = 0;

  // 3. Start Timer (Only on Core 0 to measure the macro-operation)
  if (flex_get_cluster_id() == 0 && flex_is_dm_core()) {
    start_cycles = read_cycle();
    flex_timer_start(); // Trigger GVSoC profiler if configured
  }

  // 4. Inject Heavy Traffic!
  // Every cluster simultaneously shifts data to its neighbor
  if (flex_is_dm_core()) {
    flex_dma_async_pattern_round_shift_right(local_offset, remote_offset,
                                             transfer_size);
    flex_dma_async_wait_all(); // Wait for DMA to finish
  }

  // 5. Synchronize after all DMAs are complete
  flex_global_barrier_polling();

  // 6. Stop Timer and Calculate Metrics
  if (flex_get_cluster_id() == 0 && flex_is_dm_core()) {
    flex_timer_end();
    uint32_t end_cycles = read_cycle();
    uint32_t total_cycles = end_cycles - start_cycles;

    printf("--- Performance Results ---\n");
    printf("Pattern: Round Shift Right\n");
    printf("Transfer per cluster: %d Bytes\n", transfer_size);
    printf("Total Network Transfer: %d Bytes\n",
           transfer_size * ARCH_NUM_CLUSTER);
    printf("Total Cycles: %d\n", total_cycles);
  }

  flex_eoc_all(0);
  return 0;
}