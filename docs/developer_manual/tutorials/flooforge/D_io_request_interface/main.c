#include <stdio.h>
#include <stdint.h>

int main()
{
    printf("Hello, got 0x%x from my comp\n", *(uint32_t *)0x20000000);
    printf("Got 0x%x from my comp (async)\n", *(uint32_t *)0x20000004);

    return 0;
}
