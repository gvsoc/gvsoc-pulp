#include "softhier_arch.h"
#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

// ==========================================
// Benchmark Configuration
// ==========================================
#define TILE_SIZE 1024 // 16 KB per transfer
#define TOTAL_TRANSFERS 9906
#define CYCLES_PER_PACKET 35
#define BATCH_SIZE 16 // Max outstanding txns before waiting

#define SRC_BUF 0x00000
#define DST_BUF 0x80000

uint32_t prng_state;

// Pseudo-rng
uint32_t get_random_cid() {
  prng_state ^= prng_state << 13;
  prng_state ^= prng_state >> 17;
  prng_state ^= prng_state << 5;
  return prng_state % ARCH_NUM_CLUSTER;
}

// Bare-metal cycle delay to throttle injection rate
void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; i++) {
    asm volatile("nop");
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

  prng_state = 0xDEADBEEF ^ (my_cid + 1);

  uint32_t transfers_per_cluster = TOTAL_TRANSFERS / ARCH_NUM_CLUSTER;

  if (my_cid == 0) {
    transfers_per_cluster += TOTAL_TRANSFERS % ARCH_NUM_CLUSTER;
  }

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    printf(
        "[Global Sync] Starting Controlled Uniform Random NoC Benchmark...\n");
    printf("Topology: HexaMesh (%d rings)\n", ARCH_NUM_RINGS);
    printf("Global Workload: %d total transfers (%d KB each)\n",
           TOTAL_TRANSFERS, (TILE_SIZE / 1024));
    printf("Base Transfers per cluster: %d\n",
           TOTAL_TRANSFERS / ARCH_NUM_CLUSTER);
    printf("Injection Pacing: 1 packet every %d cycles\n", CYCLES_PER_PACKET);
  }

  flex_global_barrier_polling();
  uint32_t start_time = 0;

  if (my_cid == 0 && flex_is_first_core()) {
    flex_timer_start();
    start_time = get_cycles();
  }

  if (flex_is_dm_core()) {

    uint32_t precomputed_dests[100];
    for (uint32_t k = 0; k < transfers_per_cluster; k++) {
      precomputed_dests[k] = get_random_cid();
    }

    uint32_t next_injection = get_cycles();
    uint32_t batch_counter = 0; // Track batch size without modulo

    for (uint32_t k = 0; k < transfers_per_cluster; k++) {

      if (CYCLES_PER_PACKET > 0) {
        while (get_cycles() < next_injection) {
          asm volatile("nop");
        }
        // next_injection = get_cycles() + CYCLES_PER_PACKET;
        next_injection += CYCLES_PER_PACKET;
      }

      // Fast bitwise AND instead of modulo (k & 15)
      uint32_t dst_offset = DST_BUF + ((k & 15) * TILE_SIZE);

      flex_dma_async_1d(remote_cid(precomputed_dests[k], dst_offset),
                        local(SRC_BUF), TILE_SIZE);

      // Fast batch check instead of modulo
      batch_counter++;
      if (batch_counter == BATCH_SIZE) {
        flex_dma_async_wait_all();
        batch_counter = 0;
      }
    }

    flex_dma_async_wait_all();
  }

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    flex_timer_end();
    uint32_t end_time = get_cycles();
    printf("Pure Benchmark Cycles: %u\n", (end_time - start_time));
    printf("Controlled Uniform Random Benchmark Finished Successfully!\n");
  }

  flex_global_barrier_polling();
  flex_eoc_all(0);

  return 0;
}