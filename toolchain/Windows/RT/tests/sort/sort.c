/* sort.c - qsort's probe (docs\Runtime_Design.md 11.2): records with few distinct keys, so that many compare
 * equal, sorted at widths 1 to 40 and counts 0 to 300 and some to 100,000, in random, sorted, reversed and
 * all-equal orders. Each line gives the comparisons made and a hash of the order the records end in (each carries
 * its first place), so the same object linked with GRIDE's runtime and with the UCRT prints the same lines only if
 * both leave equal records in the same order (tests\probe.ps1 sort\sort.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOST 100000
#define WIDEST 40

static unsigned char records[MOST * WIDEST];
static unsigned long seed = 4242, compares, hash;
static size_t width;

static unsigned long next(void)
{
    seed = seed * 1103515245ul + 12345ul;
    return (seed >> 16) & 0x7FFF;
}

/* the key is the record's first byte; its place is in the next four, where the width allows */
static int by_key(const void *a, const void *b)
{
    compares++;
    return (int)*(const unsigned char *)a - (int)*(const unsigned char *)b;
}

static void fill(size_t count, int order, int keys)
{
    size_t i, k;
    for (i = 0; i < count; i++) {
        unsigned char *r = records + i * width;
        for (k = 0; k < width; k++) r[k] = (unsigned char)(i >> (8 * (k % 4)));
        if (order == 0) r[0] = (unsigned char)(next() % keys);
        else if (order == 1) r[0] = (unsigned char)(i * keys / (count ? count : 1));
        else if (order == 2) r[0] = (unsigned char)((count - i) * keys / (count ? count : 1));
        else r[0] = 7;
    }
}

static void run(size_t count, int order, int keys)
{
    size_t i;
    fill(count, order, keys);
    qsort(records, count, width, by_key);
    for (i = 0; i < count * width; i++) hash = hash * 31 + records[i];
    for (i = 1; i < count; i++)
        if (records[(i - 1) * width] > records[i * width]) { printf("out of order at %u\n", (unsigned)i); break; }
}

int main(void)
{
    static const size_t widths[] = { 1, 2, 3, 4, 5, 8, 12, 16, 24, 40 };
    static const size_t big[] = { 1000, 4096, 10000, 100000 };
    unsigned w, order, keys;
    size_t n;
    for (w = 0; w < sizeof widths / sizeof widths[0]; w++) {
        width = widths[w];
        compares = hash = 0;
        for (order = 0; order < 4; order++)
            for (keys = 2; keys <= 64; keys *= 4)
                for (n = 0; n <= 300; n += (n < 40 ? 1 : 7)) run(n, (int)order, (int)keys);
        printf("width %2u: %9lu comparisons, hash %08lx\n", (unsigned)width, compares, hash & 0xFFFFFFFFul);
    }
    width = 8;
    for (w = 0; w < sizeof big / sizeof big[0]; w++)
        for (order = 0; order < 4; order++) {
            compares = hash = 0;
            run(big[w], (int)order, 200);
            printf("%6u records, order %u: %9lu comparisons, hash %08lx\n", (unsigned)big[w], order, compares,
                   hash & 0xFFFFFFFFul);
        }
    return 0;
}
