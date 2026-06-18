#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

#define TILE_SIZE 16384
#define NUM_ITERATIONS 1280
#define BATCH_SIZE 16

#define SRC_BUF 0x00000
#define DST_BUF 0x80000

uint32_t prng_state;

uint32_t get_random_cid() {
  prng_state ^= prng_state << 13;
  prng_state ^= prng_state >> 17;
  prng_state ^= prng_state << 5;
  return prng_state % ARCH_NUM_CLUSTER;
}

int main() {
  flex_global_barrier_init();

  uint32_t my_cid = flex_get_cluster_id();
  prng_state = 0xDEADBEEF ^ (my_cid + 1);

  flex_global_barrier_polling();

  if (my_cid == 0 && flex_is_first_core()) {
    flex_timer_start();
  }

  if (flex_is_dm_core() && my_cid != 63) {
    for (int k = 0; k < NUM_ITERATIONS; k++) {
      for (int b = 0; b < BATCH_SIZE; b++) {
        // uint32_t dest_cid = (my_cid + 1) % ARCH_NUM_CLUSTER; // right shift
        uint32_t dest_cid;
        do {
          dest_cid = get_random_cid();
        } while (dest_cid == my_cid);

        uint32_t dst_offset = DST_BUF + (b * TILE_SIZE);
        flex_dma_async_1d(remote_cid(dest_cid, dst_offset), local(SRC_BUF),
                          TILE_SIZE);
      }
      flex_dma_async_wait_all();
    }
  }

  flex_global_barrier_polling();
  if (my_cid == 0 && flex_is_first_core()) {
    flex_timer_end();
  }

  flex_global_barrier_polling();
  flex_eoc_all(0);

  return 0;
}