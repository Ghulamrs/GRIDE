/* debugheap10mb.c - the runtimes review's finding 3: the Debug runtime filled each block a byte at a time, 11.6 s for
   100 blocks of 10 MB against 1.1 s on Microsoft's debug heap. win\fill.asm (rep stosb) took it to 339 ms. Run on the
   Debug runtime, against a second; the bytes the fill leaves are Microsoft's (0xCD fresh, 0xDD freed is not read). */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
    clock_t start = clock();
    long ms;
    int i, fresh = 1;
    for (i = 0; i < 100; i++) {
        unsigned char *p = (unsigned char *)malloc(10 * 1024 * 1024);
        if (!p) { printf("no memory\n"); return 1; }
        if (p[0] != 0xCD || p[10 * 1024 * 1024 - 1] != 0xCD) fresh = 0;
        free(p);
    }
    ms = (long)((clock() - start) * 1000L / CLOCKS_PER_SEC);
    printf("100 blocks of 10 MB made and freed in under a second: %s\n", ms < 1000 ? "yes" : "no");
    printf("each filled with 0xCD: %s\n", fresh ? "yes" : "no");
    return 0;
}