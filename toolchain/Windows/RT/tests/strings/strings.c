/* strings.c - the string component's test (docs\Runtime_Design.md 10.2 and 11): strlen, strchr, strrchr, memchr,
 * memcmp, strcmp, strncmp, strcpy, strcat and strncpy against byte-at-a-time references, for every length to 300
 * and some to 70,000, every alignment of the arguments to 64, characters with the high bit set, and strings that
 * end on the last byte before a page that cannot be read. Each line gives the checks made, the wrong answers and a
 * hash of every answer, so the same object linked with GRIDE's runtime and with the UCRT prints the same lines
 * (tests\probe.ps1 strings\strings.c); a fault in the guard part ends the program. */
#include <stdio.h>
#include <string.h>

void *VirtualAlloc(void *at, unsigned long long size, unsigned long kind, unsigned long protect);
int VirtualProtect(void *at, unsigned long long size, unsigned long protect, unsigned long *old);

#define BIG 70000
#define PAGE 4096

static unsigned char a[BIG + 256], b[BIG + 256];
static unsigned long seed = 12345;
static unsigned long checks, wrong, hash;

static unsigned char random_byte(void)
{
    seed = seed * 1103515245ul + 12345ul;
    return (unsigned char)(1 + (seed >> 16) % 255);
}

static void fill(unsigned char *p, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) p[i] = random_byte();
}

static void answer(long value, long expected)
{
    checks++;
    if (value != expected) wrong++;
    hash = hash * 31 + (unsigned long)value;
}

static int sign(int v) { return v < 0 ? -1 : v > 0; }

static unsigned ref_strlen(const unsigned char *s) { unsigned n = 0; while (s[n]) n++; return n; }

static long ref_strchr(const unsigned char *s, int c)
{
    unsigned i;
    for (i = 0;; i++) {
        if (s[i] == (unsigned char)c) return (long)i;
        if (!s[i]) return -1;
    }
}

static long ref_strrchr(const unsigned char *s, int c)
{
    long at = -1;
    unsigned i;
    for (i = 0;; i++) {
        if (s[i] == (unsigned char)c) at = (long)i;
        if (!s[i]) return at;
    }
}

static long ref_memchr(const unsigned char *s, int c, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) if (s[i] == (unsigned char)c) return (long)i;
    return -1;
}

static int ref_memcmp(const unsigned char *x, const unsigned char *y, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

static int ref_strncmp(const unsigned char *x, const unsigned char *y, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
        if (!x[i]) return 0;
    }
    return 0;
}

static long offset(const void *found, const void *base) { return found ? (long)((const char *)found - (const char *)base) : -1; }

static void report(const char *name)
{
    printf("%-8s %8lu checks, %lu wrong, hash %08lx\n", name, checks, wrong, hash & 0xFFFFFFFFul);
    checks = wrong = hash = 0;
}

static const unsigned lengths[] = { 1000, 4095, 4096, 4097, 8191, 65535, 69999 };

static unsigned length_at(unsigned i) { return i <= 300 ? i : lengths[i - 301]; }
#define LENGTHS (301 + sizeof lengths / sizeof lengths[0])

static void test_strlen(void)
{
    unsigned al, i;
    for (al = 0; al < 64; al++)
        for (i = 0; i < LENGTHS; i++) {
            unsigned n = length_at(i);
            fill(a + al, n);
            a[al + n] = 0;
            answer((long)strlen((char *)a + al), (long)n);
        }
    report("strlen");
}

static void test_strchr(int last)
{
    unsigned al, n, k;
    for (al = 0; al < 64; al++)
        for (n = 0; n <= 200; n += (n < 40 ? 1 : 7)) {
            fill(a + al, n);
            a[al + n] = 0;
            for (k = 0; k <= n; k += (n < 20 ? 1 : 5)) {
                int c = k < n ? a[al + k] : 0xC1;
                long got = offset(last ? strrchr((char *)a + al, c) : strchr((char *)a + al, c), a + al);
                answer(got, last ? ref_strrchr(a + al, c) : ref_strchr(a + al, c));
            }
            answer(offset(last ? strrchr((char *)a + al, 0) : strchr((char *)a + al, 0), a + al), (long)n);
        }
    report(last ? "strrchr" : "strchr");
}

static void test_memchr(void)
{
    unsigned al, n, k;
    for (al = 0; al < 64; al++)
        for (n = 0; n <= 300; n += (n < 70 ? 1 : 9)) {
            fill(a + al, n + 32);
            for (k = 0; k < n + 32; k += (n < 20 ? 1 : 3)) {
                int c = a[al + k];
                answer(offset(memchr(a + al, c, n), a + al), ref_memchr(a + al, c, n));
            }
            answer(offset(memchr(a + al, 0, n), a + al), ref_memchr(a + al, 0, n));
        }
    for (n = 0; n < sizeof lengths / sizeof lengths[0]; n++) {
        fill(a, lengths[n] + 1);
        a[lengths[n]] = 0;
        answer(offset(memchr(a, 0, lengths[n] + 1), a), (long)lengths[n]);
    }
    report("memchr");
}

static void test_memcmp(void)
{
    unsigned x, y, n, k;
    for (x = 0; x < 32; x++)
        for (y = 0; y < 32; y += (x < 4 ? 1 : 5))
            for (n = 0; n <= 200; n += (n < 40 ? 1 : 11)) {
                fill(a + x, n);
                memcpy(b + y, a + x, n);
                answer(sign(memcmp(a + x, b + y, n)), 0);
                hash = hash * 31 + (unsigned long)memcmp(a + x, b + y, n);
                for (k = 0; k < n; k += (n < 16 ? 1 : 7)) {
                    unsigned char keep = b[y + k];
                    b[y + k] = (unsigned char)(keep ^ 0x80);
                    answer(sign(memcmp(a + x, b + y, n)), ref_memcmp(a + x, b + y, n));
                    hash = hash * 31 + (unsigned long)memcmp(a + x, b + y, n);
                    b[y + k] = keep;
                }
            }
    for (n = 0; n < sizeof lengths / sizeof lengths[0]; n++) {
        fill(a, lengths[n]);
        memcpy(b, a, lengths[n]);
        b[lengths[n] - 1] ^= 1;
        answer(sign(memcmp(a, b, lengths[n])), ref_memcmp(a, b, lengths[n]));
    }
    report("memcmp");
}

static void test_strcmp(int bounded)
{
    unsigned x, y, n, k;
    for (x = 0; x < 32; x++)
        for (y = 0; y < 32; y += (x < 4 ? 1 : 5))
            for (n = 0; n <= 150; n += (n < 40 ? 1 : 11)) {
                fill(a + x, n);
                a[x + n] = 0;
                memcpy(b + y, a + x, n + 1);
                for (k = 0; k <= n; k += (n < 16 ? 1 : 7)) {
                    unsigned char keep = b[y + k];
                    unsigned limit = bounded ? k + (k & 1) : 0;
                    if (k < n) b[y + k] = (unsigned char)(keep ^ 0x80);
                    else { b[y + k] = 'x'; b[y + k + 1] = 0; }
                    if (bounded) {
                        int got = strncmp((char *)a + x, (char *)b + y, limit);
                        answer(sign(got), ref_strncmp(a + x, b + y, limit));
                        hash = hash * 31 + (unsigned long)got;
                    } else {
                        int got = strcmp((char *)a + x, (char *)b + y);
                        answer(sign(got), ref_strncmp(a + x, b + y, n + 2));
                        hash = hash * 31 + (unsigned long)got;
                    }
                    b[y + k] = keep;
                    if (k == n) b[y + k + 1] = random_byte();
                }
                b[y + n] = 0;
                answer(sign(bounded ? strncmp((char *)a + x, (char *)b + y, n + 5) : strcmp((char *)a + x, (char *)b + y)), 0);
            }
    for (n = 0; n < sizeof lengths / sizeof lengths[0]; n++) {
        fill(a, lengths[n]);
        a[lengths[n]] = 0;
        memcpy(b, a, lengths[n] + 1);
        answer(sign(strcmp((char *)a, (char *)b)), 0);
        b[lengths[n] - 1] = 1;
        answer(sign(strcmp((char *)a, (char *)b)), ref_strncmp(a, b, lengths[n] + 1));
    }
    report(bounded ? "strncmp" : "strcmp");
}

static void test_copies(void)
{
    unsigned x, y, n, m;
    for (x = 0; x < 32; x++)
        for (y = 0; y < 32; y += (x < 4 ? 1 : 3))
            for (n = 0; n <= 200; n += (n < 40 ? 1 : 13)) {
                char *got;
                fill(a + x, n);
                a[x + n] = 0;
                fill(b, 600);
                got = strcpy((char *)b + y, (char *)a + x);
                answer(offset(got, b + y), 0);
                answer(memcmp(b + y, a + x, n + 1) == 0, 1);
                m = n / 3;
                b[y + m] = 0;
                got = strcat((char *)b + y, (char *)a + x);
                answer(offset(got, b + y), 0);
                answer(ref_strlen(b + y), (long)(m + n));
                answer(memcmp(b + y + m, a + x, n + 1) == 0, 1);
                fill(b, 600);
                got = strncpy((char *)b + y, (char *)a + x, n + 7);
                answer(offset(got, b + y), 0);
                answer(memcmp(b + y, a + x, n) == 0 && b[y + n] == 0 && b[y + n + 6] == 0, 1);
                hash = hash * 31 + b[y + n + 7];
            }
    report("copies");
}

/* the guard part: each string or block ends on the last byte before a page that cannot be read */
static void test_guard(void)
{
    unsigned char *region = (unsigned char *)VirtualAlloc(0, 4 * PAGE, 0x3000, 4);
    unsigned char *end1, *end2;
    unsigned long old;
    unsigned n, al;
    if (!region || !VirtualProtect(region + PAGE, PAGE, 1, &old) || !VirtualProtect(region + 3 * PAGE, PAGE, 1, &old)) {
        printf("guard: no pages\n");
        return;
    }
    end1 = region + PAGE;
    end2 = region + 3 * PAGE;
    for (n = 0; n <= 300; n++)
        for (al = 0; al < 16; al++) {
            unsigned char *s = end1 - n - 1, *t = end2 - n - 1 - al;
            fill(s, n);
            s[n] = 0;
            memcpy(t, s, n + 1);
            answer((long)strlen((char *)s), (long)n);
            answer(offset(strchr((char *)s, 0xC1), s), ref_strchr(s, 0xC1));
            answer(offset(strrchr((char *)s, n ? s[0] : 0), s), ref_strrchr(s, n ? s[0] : 0));
            answer(offset(memchr(s, 0xC1, n + 1), s), ref_memchr(s, 0xC1, n + 1));
            answer(sign(memcmp(s, t, n + 1)), 0);
            answer(sign(strcmp((char *)s, (char *)t)), 0);
            answer(sign(strcmp((char *)t, (char *)s)), 0);
            answer(sign(strncmp((char *)s, (char *)t, n + 9)), 0);
            if (n > 0) {
                t[n - 1] ^= 0x80;
                answer(sign(strcmp((char *)s, (char *)t)), ref_strncmp(s, t, n + 1));
                t[n - 1] ^= 0x80;
            }
        }
    report("guard");
}

int main(void)
{
    test_strlen();
    test_strchr(0);
    test_strchr(1);
    test_memchr();
    test_memcmp();
    test_strcmp(0);
    test_strcmp(1);
    test_copies();
    test_guard();
    return 0;
}
