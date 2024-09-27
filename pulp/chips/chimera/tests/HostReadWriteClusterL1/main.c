#include "pulp.h"

int main(){
    volatile int32_t* ptr_cluster = (int32_t*)0x10000000; 
    *ptr_cluster = 42;
    volatile int32_t value = *ptr_cluster;

    return value == 42 ? 0 : 1;
}