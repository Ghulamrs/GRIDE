/* debugheap.c - the Debug runtime's heap, as the Microsoft runtime's debug heap lays it out (docs\Runtime_Design.md
 * 4.3): a new block's bytes are 0xCD, calloc's zero, the four bytes after a block 0xFD; realloc keeps the old bytes and
 * makes the new ones 0xCD. Run by probe.ps1 -Debug against the Debug UCRT's transcript (debugheap.debug.expected). */
#include <stdio.h>
#include <stdlib.h>

static void show(const char *what, const unsigned char *p, int n)
{
    int i;
    printf("%s:", what);
    for (i = 0; i < n; i++) printf(" %02x", p[i]);
    printf("\n");
}

int main(void)
{
    unsigned char *a = (unsigned char *)malloc(8);
    unsigned char *b = (unsigned char *)calloc(2, 4);
    unsigned char *c;
    show("malloc(8)", a, 8);
    show("after it", a + 8, 4);
    show("calloc(2, 4)", b, 8);
    a[0] = 1;
    a[7] = 7;
    c = (unsigned char *)realloc(a, 12);
    show("realloc(12)", c, 12);
    show("after it", c + 12, 4);
    free(b);
    free(c);
    printf("freed\n");
    return 0;
}
