#ifndef _FLEX_RUNTIME_H_
#define _FLEX_RUNTIME_H_
#include <stdint.h>
#include "softhier_arch.h"

#define BARRIER_OFFSET              (ARCH_CLUSTER_TCDM_BASE+ARCH_CLUSTER_TCDM_SIZE-8)
#define BARRIER_ITER_OFFSET         (ARCH_CLUSTER_TCDM_BASE+ARCH_CLUSTER_TCDM_SIZE-16)
#define local(offset)               (ARCH_CLUSTER_TCDM_BASE+offset)
#define zomem(offset)               (ARCH_CLUSTER_ZOMEM_BASE+offset)
#define remote_cid(cid,offset)      (ARCH_CLUSTER_TCDM_REMOTE+cid*ARCH_CLUSTER_TCDM_SIZE+offset)

/*******************
*  Core Position   *
*******************/

uint32_t flex_get_cluster_id(){
    volatile uint32_t * cluster_reg      = (volatile uint32_t *) ARCH_CLUSTER_REG_BASE;
    return *cluster_reg;
}

uint32_t flex_get_core_id(){
    uint32_t hartid;
    asm volatile("csrr %0, mhartid" : "=r"(hartid));
    return hartid;
}

uint32_t flex_is_dm_core(){
    uint32_t hartid;
    asm volatile("csrr %0, mhartid" : "=r"(hartid));
    return (hartid == ARCH_NUM_CORE_PER_CLUSTER-1);
}

uint32_t flex_is_first_core(){
    uint32_t hartid;
    asm volatile("csrr %0, mhartid" : "=r"(hartid));
    return (hartid == 0);
}

/*******************
*  Global Barrier  *
*******************/

void flex_intra_cluster_sync(){
    asm volatile("csrr x0, 0x7C2" ::: "memory");
}

uint32_t flex_get_enable_value(){
    volatile uint32_t * amo_reg      = (volatile uint32_t *) (ARCH_CLUSTER_REG_BASE+4);
    return *amo_reg;
}

uint32_t flex_get_disable_value(){
    volatile uint32_t * amo_reg      = (volatile uint32_t *) (ARCH_CLUSTER_REG_BASE+8);
    return *amo_reg;
}

void flex_reset_barrier(uint32_t* barrier){
    *barrier = flex_get_disable_value();
}

uint32_t flex_amo_fetch_add(uint32_t* barrier){
    return __atomic_fetch_add(barrier, flex_get_enable_value(), __ATOMIC_RELAXED);
}

void flex_global_barrier_init(){
    volatile uint32_t * barrier      = (volatile uint32_t *)local(BARRIER_OFFSET);

    flex_intra_cluster_sync();

    if (flex_is_dm_core()){
        flex_reset_barrier(barrier);
    }

    flex_intra_cluster_sync();
}

void flex_global_barrier_polling(){
    volatile uint32_t * barrier      = (volatile uint32_t *)remote_cid(0, BARRIER_OFFSET);
    volatile uint32_t * barrier_iter = (volatile uint32_t *)remote_cid(0, BARRIER_ITER_OFFSET);

    flex_intra_cluster_sync();

    if (flex_is_dm_core()){
        // Remember previous iteration
        uint32_t prev_barrier_iteration = *barrier_iter;

        if ((ARCH_NUM_CLUSTER - flex_get_enable_value()) == flex_amo_fetch_add(barrier)) {
            flex_reset_barrier(barrier);
            flex_amo_fetch_add(barrier_iter);
        } else {
            while((*barrier_iter) == prev_barrier_iteration);
        }
    }

    flex_intra_cluster_sync();
}


/*******************
*        EoC       *
*******************/

void flex_eoc(uint32_t val){
    volatile uint32_t * eoc_reg = (volatile uint32_t *) ARCH_SOC_REGISTER_EOC;
    *eoc_reg = val;
}

void flex_eoc_all(uint32_t val){
    volatile uint32_t * eoc_reg = (volatile uint32_t *) (ARCH_SOC_REGISTER_EOC + 4);
    *eoc_reg = val;
}

/*******************
*   Perf Counter   *
*******************/

void flex_timer_start(){
    volatile uint32_t * start_reg    = (volatile uint32_t *) (ARCH_SOC_REGISTER_EOC + 8);
    *start_reg = 1;
}

void flex_timer_end(){
    volatile uint32_t * end_reg = (volatile uint32_t *) (ARCH_SOC_REGISTER_EOC + 12);
    *end_reg = 1;
}

/*******************
*      Logging     *
*******************/

void flex_log_char(char c){
    uint32_t data = (uint32_t) c;
    volatile uint32_t * log_reg = (volatile uint32_t *)(ARCH_SOC_REGISTER_EOC + 16);
    *log_reg = data;
}

void flex_print(char * str){
    for (int i = 0; str[i] != '\0'; i++) {
        flex_log_char(str[i]);
    }
}

void flex_print_int(uint32_t data){
    volatile uint32_t * log_reg = (volatile uint32_t *)(ARCH_SOC_REGISTER_EOC + 20);
    *log_reg = data;
}

#endif