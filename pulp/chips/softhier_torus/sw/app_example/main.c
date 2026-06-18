#include "softhier_dma_pattern.h"
#include "softhier_printf.h"
#include "softhier_runtime.h"

int main() {
  flex_global_barrier_init();

  uint32_t my_cid = flex_get_cluster_id();

  printf("Cluster %d\n", my_cid);

  flex_global_barrier_polling();
  flex_eoc_all(0);

  return 0;
}