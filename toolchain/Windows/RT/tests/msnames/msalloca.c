/* msalloca.c - _alloca and alloca, the compilers' own (docs\Runtime_Design.md 9.4): c90 compiles this file and
   cpp11 msalloca.cpp, which includes it, and both print what the UCRT's run of c90's did. Blocks in a loop, each kept
   until the function returns; calls of twelve arguments after a block and around one, nested, and a block made while
   an expression has values pushed and a call's arguments half built; recursion with a block at every level; a block
   of half a megabyte, which the pages are probed for; and, first, while the stack is still fresh, two hundred blocks
   nothing touches before the next call. Nothing here prints an address: alignment and overlap are printed as answers. */
#include <stdio.h>
#include <string.h>
#include <malloc.h>

static unsigned sum(const unsigned char *p, size_t n)
{
    unsigned s = 0;
    size_t i;
    for (i = 0; i < n; i++) s = s * 31u + p[i];
    return s;
}

static void fill(unsigned char *p, size_t n, int seed)
{
    size_t i;
    for (i = 0; i < n; i++) p[i] = (unsigned char)(seed + i * 7);
}

static int intact(const unsigned char *p, size_t n, int seed)
{
    size_t i;
    for (i = 0; i < n; i++) if (p[i] != (unsigned char)(seed + i * 7)) return 0;
    return 1;
}

static long twelve(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j, long k, long l)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j + 11 * k + 12 * l;
}

static long word(const void *p) { return (long)sum((const unsigned char *)p, 16); }

/* 24 blocks of 1 to 852 bytes, each filled; all still there after the loop, 8-aligned, and none overlapping */
static void loop(void)
{
    unsigned char *blocks[24];
    size_t i, k, n, m;
    int ok = 1, aligned = 1, apart = 1;
    for (i = 0; i < 24; i++) {
        n = i * 37 + 1;
        blocks[i] = (unsigned char *)_alloca(n);
        fill(blocks[i], n, (int)i);
        if ((size_t)blocks[i] % 8 != 0) aligned = 0;
    }
    for (i = 0; i < 24; i++) if (!intact(blocks[i], i * 37 + 1, (int)i)) ok = 0;
    for (i = 0; i < 24; i++)
        for (k = i + 1; k < 24; k++) {
            n = i * 37 + 1;
            m = k * 37 + 1;
            if ((size_t)blocks[i] < (size_t)blocks[k] + m && (size_t)blocks[k] < (size_t)blocks[i] + n) apart = 0;
        }
    printf("loop: 24 blocks, intact %d, 8-aligned %d, apart %d\n", ok, aligned, apart);
}

/* calls after a block, with stack arguments, nested; a block whose size a call answers */
static void calls(int n, const char *text)
{
    unsigned char *p = (unsigned char *)alloca(n);
    char *copy = (char *)_alloca(strlen(text) + 1);
    long r;
    fill(p, n, 5);
    strcpy(copy, text);
    r = twelve(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, twelve(12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1));
    printf("calls: twelve %ld, block intact %d, copy \"%s\"\n", r, intact(p, n, 5), copy);
    r = twelve(twelve(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1), 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, (long)strlen(copy));
    printf("calls: nested %ld, block intact %d\n", r, intact(p, n, 5));
}

/* blocks made inside expressions: as a call's last argument, as a middle one while the arguments are being built,
   and as an operand with the other's value held */
static void inside(void)
{
    unsigned char *keep = (unsigned char *)_alloca(40);
    long r;
    fill(keep, 40, 9);
    r = twelve(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, word(memset(_alloca(100), 3, 100)));
    printf("inside: last argument %ld\n", r);
    r = twelve(1, 2, 3, 4, 5, word(memset(_alloca(64), 4, 64)), 7, 8, 9, 10, twelve(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1), 12);
    printf("inside: a middle argument %ld\n", r);
    r = twelve(2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2) * 3 + word(memset(alloca(48), 7, 48)) - twelve(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    printf("inside: an operand %ld\n", r);
    printf("inside: the first block intact %d\n", intact(keep, 40, 9));
}

/* a block at every level, checked after the levels below have made theirs and returned */
static unsigned recurse(int depth)
{
    size_t n = (size_t)depth * 24 + 8;
    unsigned char *p = (unsigned char *)_alloca(n);
    unsigned below;
    fill(p, n, depth);
    below = depth > 0 ? recurse(depth - 1) : 0;
    if (!intact(p, n, depth)) return 0xdeadu;
    return below * 3u + sum(p, n);
}

/* half a megabyte: on Windows the pages are probed before the stack pointer passes them */
static void large(void)
{
    size_t n = 500000;
    unsigned char *p = (unsigned char *)_alloca(n);
    memset(p, 0xa5, n);
    p[0] = 1;
    p[n - 1] = 2;
    printf("large: %u bytes, sum %u\n", (unsigned)n, sum(p, n));
}

/* two hundred blocks of 1000 bytes nothing touches, then a call below them all */
static void untouched(void)
{
    int i;
    unsigned char *p = 0;
    for (i = 0; i < 200; i++) p = (unsigned char *)alloca(1000);
    fill(p, 1000, 1);
    printf("untouched: 200 blocks, then a call: %ld, last intact %d\n",
           twelve(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12), intact(p, 1000, 1));
}

int main(void)
{
    untouched();
    loop();
    calls(300, "a block whose size strlen gave");
    inside();
    printf("recursion: %u\n", recurse(40));
    large();
    printf("alloca(0): %s\n", alloca(0) != NULL ? "a pointer" : "null");
    return 0;
}
