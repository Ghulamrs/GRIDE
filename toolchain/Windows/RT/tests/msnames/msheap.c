/* msheap.c - Microsoft's heap names, _msize, _aligned_malloc and _aligned_free, as the UCRT answers them
   (docs\Runtime_Design.md 9.4). _msize is printed as a bound: GRIDE's Release heaps answer a block's usable size,
   at least the size asked for, where the UCRT answers the size asked for (the recorded deviation). */
#include <stdio.h>
#include <string.h>
#include <malloc.h>

int main(void)
{
    static const size_t sizes[] = { 1, 10, 16, 100, 4000, 5000, 100000 };
    static const size_t alignments[] = { 1, 2, 8, 16, 64, 256, 4096 };
    size_t i, k;
    char *p;
    void *a;

    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        p = (char *)malloc(sizes[i]);
        memset(p, 'x', sizes[i]);
        printf("_msize(malloc(%u)) at least %u: %d\n", (unsigned)sizes[i], (unsigned)sizes[i], _msize(p) >= sizes[i]);
        p = (char *)realloc(p, sizes[i] * 3);
        printf("  after realloc to %u: %d, kept %d\n", (unsigned)(sizes[i] * 3), _msize(p) >= sizes[i] * 3,
               p[sizes[i] - 1] == 'x');
        free(p);
    }
    p = (char *)calloc(7, 9);
    printf("_msize(calloc(7, 9)) at least 63: %d\n", _msize(p) >= 63);
    free(p);

    for (k = 0; k < sizeof alignments / sizeof alignments[0]; k++) {
        for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            a = _aligned_malloc(sizes[i], alignments[k]);
            if (!a) { printf("_aligned_malloc(%u, %u): null\n", (unsigned)sizes[i], (unsigned)alignments[k]); continue; }
            memset(a, 'y', sizes[i]);
            if ((unsigned long)(size_t)a % alignments[k] != 0)
                printf("_aligned_malloc(%u, %u): not aligned\n", (unsigned)sizes[i], (unsigned)alignments[k]);
            _aligned_free(a);
        }
        printf("_aligned_malloc at %u: %u sizes aligned and freed\n", (unsigned)alignments[k],
               (unsigned)(sizeof sizes / sizeof sizes[0]));
    }
    a = _aligned_malloc(0, 32);
    printf("_aligned_malloc(0, 32): %s\n", a ? "a block" : "null");
    _aligned_free(a);
    _aligned_free(NULL);
    printf("_aligned_free(NULL): nothing\n");
    return 0;
}
