/* deep200.c - the runtimes review's finding 7: the C6747's link defaults were a 16 KB stack and a 1 MB heap on a
   64 MB RAM, with nothing checked; two hundred recursive calls with 256-byte frames overwrote what lay below the
   stack and sim6747 stopped on an undefined instruction. The defaults are 1 MB and 16 MB now (toolchain.json's
   ti-link, the linker command file c6747.ps1 writes). And 64 blocks of 16,000 bytes no longer fill the heap. */
#include <stdio.h>
#include <stdlib.h>

static int deep(int n)
{
    volatile unsigned char frame[256];
    int i;
    for (i = 0; i < 256; i++) frame[i] = (unsigned char)(n + i);
    if (n == 0) return frame[255];
    return deep(n - 1) + frame[n % 256] % 7;
}

int main(void)
{
    int i, got = 0;
    void *blocks[100];
    printf("deep(200) = %d\n", deep(200));
    for (i = 0; i < 100; i++) {
        blocks[i] = malloc(16000);
        if (blocks[i]) got++;
    }
    printf("blocks of 16,000 bytes: %d of 100\n", got);
    for (i = 0; i < 100; i++) free(blocks[i]);
    return 0;
}