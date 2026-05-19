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
