#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"
#include "softhier_arch.h"

// ==========================================
// Benchmark Configuration (modifiable by Python)
// ==========================================
#define TILE_SIZE 1024
#define TOTAL_TRANSFERS 125000
#define CYCLES_PER_PACKET 16
#define BATCH_SIZE 16

#define SRC_BUF 0x00000
#define DST_BUF 0x80000

#define MAX_TRANSFERS_PER_CLUSTER 10000

// Bare-metal cycle delay
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
  uint32_t transfers_per_cluster = TOTAL_TRANSFERS / ARCH_NUM_CLUSTER;

  if (my_cid == 0) {
    transfers_per_cluster += TOTAL_TRANSFERS % ARCH_NUM_CLUSTER;
  }

  // Declared locally on the stack to prevent any global L2 memory overwrites or access overhead
  uint32_t dest_cids[MAX_TRANSFERS_PER_CLUSTER];

  // Pre-generate all random destinations locally
  if (flex_is_dm_core()) {
    uint32_t prng_state = 0xDEADBEEF ^ (my_cid + 1);
    for (uint32_t k = 0; k < transfers_per_cluster && k < MAX_TRANSFERS_PER_CLUSTER; k++) {
      uint32_t dest_cid;
      do {
        prng_state ^= prng_state << 13;
        prng_state ^= prng_state >> 17;
        prng_state ^= prng_state << 5;
        dest_cid = prng_state % ARCH_NUM_CLUSTER;
      } while (dest_cid == my_cid);
      dest_cids[k] = dest_cid;
    }
  }

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    printf("[Global Sync] Starting Random Traffic Bandwidth Benchmark...\n");
    printf("Topology: 3D Torus (%dx%dx%d)\n", ARCH_NUM_CLUSTER_X,
           ARCH_NUM_CLUSTER_Y, ARCH_NUM_CLUSTER_Z);
    printf("Global Workload: %d total transfers (%d B each)\n",
           TOTAL_TRANSFERS, TILE_SIZE);
    printf("Base Transfers per cluster: %d\n",
           transfers_per_cluster);
    printf("Injection Pacing: 1 packet every %d cycles\n", CYCLES_PER_PACKET);
  }

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    flex_timer_start();
  }

  // Start time measurement
  uint32_t start_cycles = get_cycles();

  if (flex_is_dm_core()) {
    uint32_t next_injection = start_cycles;
    
    // Variables for latency tracking
    uint32_t total_app_latency = 0; 
    uint32_t batch_ideal_times[BATCH_SIZE];
    uint32_t batch_idx = 0;

    for (uint32_t k = 0; k < transfers_per_cluster; k++) {
      
      // Ruthless Open-Loop Pacing
      if (CYCLES_PER_PACKET > 0) {
        while (get_cycles() < next_injection) { 
          asm volatile("nop"); 
        }
        
        // Record the exact cycle this packet was scheduled to leave
        batch_ideal_times[batch_idx++] = next_injection;
        
        // Force the timeline forward, accumulating debt if we are behind
        next_injection += CYCLES_PER_PACKET;
      }

      uint32_t dest_cid = dest_cids[k];
      uint32_t dst_offset = DST_BUF + ((k & 15) << 6);

      flex_dma_async_1d(remote_cid(dest_cid, dst_offset), local(SRC_BUF), TILE_SIZE);

      // Batch Wait & Latency Calculation
      if ((k + 1) % BATCH_SIZE == 0) {
        flex_dma_async_wait_all();
        uint32_t batch_complete_time = get_cycles();
        
        // Accumulate the true latency for every packet in this batch
        total_app_latency += (batch_complete_time - batch_ideal_times[batch_idx - 1]);
        batch_idx = 0; // Reset index for the next batch
      }
    }

    // Clean up any remaining packets if total transfers wasn't a perfect multiple of BATCH_SIZE
    if (batch_idx > 0) {
      flex_dma_async_wait_all();
      uint32_t batch_complete_time = get_cycles();
      total_app_latency += (batch_complete_time - batch_ideal_times[batch_idx - 1]);
    }
    if (my_cid == 0) {
      uint32_t total_batches = (transfers_per_cluster + BATCH_SIZE - 1) / BATCH_SIZE;
      uint32_t avg_lat_int = total_app_latency / total_batches;
      uint32_t avg_lat_frac = ((total_app_latency * 100) / total_batches) % 100;
      printf("C-Measured Avg App Latency: %u.%02u cycles\n", avg_lat_int, avg_lat_frac);
    }
  }

  // End time measurement (ensure all clusters finish all transfers)
  flex_global_barrier_polling();
  uint32_t end_cycles = get_cycles();

  if (my_cid == 0 && flex_is_first_core()) {
    flex_timer_end();
    uint32_t total_cycles = end_cycles - start_cycles;
    uint32_t total_flits = ((uint32_t)TOTAL_TRANSFERS * TILE_SIZE * 8) / ARCH_NOC_LINK_WIDTH;
    uint32_t denom = (uint32_t)ARCH_NUM_CLUSTER * total_cycles;
    
    uint32_t val_int = 0;
    uint32_t val_frac = 0;
    if (denom > 0) {
      val_int = total_flits / denom;
      uint32_t rem = total_flits % denom;
      uint32_t f1 = (rem * 10) / denom; rem = (rem * 10) % denom;
      uint32_t f2 = (rem * 10) / denom; rem = (rem * 10) % denom;
      uint32_t f3 = (rem * 10) / denom; rem = (rem * 10) % denom;
      uint32_t f4 = (rem * 10) / denom;
      
      val_frac = f1 * 1000 + f2 * 100 + f3 * 10 + f4;
    }

    printf("C-Achieved Bandwidth: %u.%04u Flits/Cluster/Cycle\n", val_int, val_frac);
    printf("C-Total Cycles: %u\n", total_cycles);
    printf("Controlled Uniform Random Benchmark Finished Successfully!\n");
  }

  flex_global_barrier_polling();
  flex_eoc_all(0);

  return 0;
}
