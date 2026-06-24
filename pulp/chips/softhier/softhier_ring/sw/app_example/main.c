#include "softhier_runtime.h"
#include "softhier_printf.h"
#include "softhier_dma_pattern.h"

int main()
{
    flex_global_barrier_init();
    for(int i=0; i < 10; i++){
        flex_global_barrier_polling();
        if(flex_get_cluster_id() == 0 && flex_is_first_core()){
            printf("[Global Sync] iter %d\n", i);
        }
    }
    flex_global_barrier_polling();
    if(flex_get_cluster_id() == 0 && flex_is_dm_core()){
        // DMA Read Data From Neighbor Cluster 
        flex_dma_async_1d(local(0), remote_cid(1,0), 1024);
        flex_dma_async_wait_all();
        printf("[DMA Pattern] Access Neighbor \n");
    }
    flex_global_barrier_polling();
    if(flex_is_dm_core()){
        // DMA Row-wise Round Shift 
        flex_dma_async_pattern_round_shift_right(0, 0, 1024);
        flex_dma_async_wait_all();
        if(flex_get_cluster_id() == 0) printf("[DMA Pattern] Round Shift \n");
    }
    flex_global_barrier_polling();
    if(flex_is_dm_core()){
        // DMA Row-wise Round Shift 
        flex_dma_async_pattern_dialog_to_dialog(0, 0, 1024);
        flex_dma_async_wait_all();
        if(flex_get_cluster_id() == 0) printf("[DMA Pattern] Dialog Transfer \n");
    }
    flex_global_barrier_polling();
    flex_eoc_all(0);
    return 0;
}