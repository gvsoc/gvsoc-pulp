#include <stdio.h>
#include <stdint.h>

inline uint32_t snrt_mcycle() {
    uint32_t register r;
    asm volatile("csrr %0, mcycle" : "=r"(r) : : "memory");
    return r;
}

#define BASE_ADDR 0xA0010000

static __attribute__((noinline)) void bench(uint32_t *result, uint32_t *diff_0, uint32_t *diff_1)
{
    // Snitch has 8 outstanding request, and each is 1000 cycles
    // The first 8 should not block but the 8 following should block
    // Then the final reads should block on all accesses,
    // So we should get around 1000 cycles for first step and 2000 for last step
    uint32_t start = snrt_mcycle();
    uint32_t a = *(volatile int *)(BASE_ADDR + 0);
    uint32_t b = *(volatile int *)(BASE_ADDR + 4);
    uint32_t c = *(volatile int *)(BASE_ADDR + 8);
    uint32_t d = *(volatile int *)(BASE_ADDR + 12);
    uint32_t e = *(volatile int *)(BASE_ADDR + 16);
    uint32_t f = *(volatile int *)(BASE_ADDR + 20);
    uint32_t g = *(volatile int *)(BASE_ADDR + 24);
    uint32_t h = *(volatile int *)(BASE_ADDR + 28);
    uint32_t i = *(volatile int *)(BASE_ADDR + 32);
    uint32_t j = *(volatile int *)(BASE_ADDR + 36);
    uint32_t k = *(volatile int *)(BASE_ADDR + 40);
    uint32_t l = *(volatile int *)(BASE_ADDR + 44);
    uint32_t m = *(volatile int *)(BASE_ADDR + 48);
    uint32_t n = *(volatile int *)(BASE_ADDR + 52);
    uint32_t o = *(volatile int *)(BASE_ADDR + 56);
    uint32_t p = *(volatile int *)(BASE_ADDR + 60);

    *diff_0 = snrt_mcycle() - start;

    *result = a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p;

    *diff_1 = snrt_mcycle() - start;
}

int main()
{
    uint32_t diff_0, diff_1;
    uint32_t result;
    // Iterate a bit to avoid cache issues
    for (int i=0; i<4; i++)
    {
        bench(&result, &diff_0, &diff_1);
    }

    printf("The first step took %d cycles\n", diff_0);
    printf("The second step took %d cycles\n", diff_1);
}
