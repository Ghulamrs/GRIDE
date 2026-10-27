/* bench.c - the runtime's benchmarks (docs\Runtime_Design.md 10.5): one C program, each test named on its
 * command line, timing itself with clock() and printing "<test> <milliseconds>". bench.ps1 builds it twice from
 * the same objects - once against GRIDE's runtime, once against Microsoft's static CRT - and runs each test five
 * times on each, keeping the best. Each test also prints a checksum, so that the two runtimes are seen to have
 * done the same work. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MB (1024L * 1024L)

static char big[MB + 64], other[MB + 64];
static unsigned long sink;

static int compare_ints(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return x < y ? -1 : x > y;
}

static unsigned long next_random(unsigned long *state)
{
    *state = *state * 1103515245UL + 12345UL;
    return (*state >> 8) & 0xFFFFFFUL;
}

static long printf_file(const char *kind)
{
    FILE *f = fopen("bench-out.txt", "w");
    long i;
    for (i = 0; i < 1000000L; i++) {
        if (kind[0] == 'i') fprintf(f, "%ld %d\n", i, (int)(i * 7));
        else if (kind[0] == 'f') fprintf(f, "%.6f %g\n", i * 0.37, i / 3.0);
        else fprintf(f, "%s:%s\n", "name", "a longer value for the string test");
    }
    i = ftell(f);
    fclose(f);
    remove("bench-out.txt");
    return i;
}

static long printf_stdout(void)
{
    long i;
    for (i = 0; i < 1000000L; i++) printf("%ld %s\n", i, "to the null device");
    return i;
}

static long make_file(int text)
{
    FILE *f = fopen("bench-big.bin", text ? "w" : "wb");
    long i;
    for (i = 0; i < 64; i++) {
        if (text) {
            long j;
            for (j = 0; j < 16384; j++) fputs("a line of text, 63 characters long, written for fgets to read.\n", f);
        } else {
            fwrite(big, 1, MB, f);
        }
    }
    fclose(f);
    return 64;
}

static long read_file(int text)
{
    FILE *f = fopen("bench-big.bin", text ? "r" : "rb");
    long n = 0;
    char line[256];
    if (text) {
        while (fgets(line, sizeof line, f)) n++;
    } else {
        size_t got;
        while ((got = fread(other, 1, 65536, f)) > 0) n += (long)got;
    }
    fclose(f);
    return n;
}

/* a byte at a time: putc writing a 64 MB binary file, getc reading make_file's back */
static long characters(int reading)
{
    FILE *f = fopen("bench-big.bin", reading ? "rb" : "wb");
    long n = 0, i;
    int c;
    if (reading) {
        while ((c = getc(f)) != EOF) n += c;
    } else {
        for (i = 0; i < 64 * MB; i++) putc((int)(i & 255), f);
        n = ftell(f);
    }
    fclose(f);
    return n;
}

static long memory(const char *what, long size)
{
    long total = 512L * MB, i, rounds = total / size;
    for (i = 0; i < rounds; i++) {
        long at = (i * 64) & 4095;
        if (what[3] == 'c') memcpy(other + (size < 8192 ? at : 0), big + (size < 8192 ? at : 0), (size_t)size);
        else if (what[3] == 's') memset(big + (size < 8192 ? at : 0), (int)(i & 255), (size_t)size);
        else memmove(big + 1, big, (size_t)size);
    }
    return (long)(unsigned char)other[size / 2] + (long)(unsigned char)big[size / 3];
}

static long strings(const char *what)
{
    long i, n = 0;
    memset(big, 'x', 1000);
    big[1000] = 0;
    memcpy(other, big, 1001);
    big[999] = 'y';
    other[999] = 'y';
    for (i = 0; i < 2000000L; i++) {
        big[i & 511] = (char)('a' + (i & 15));
        other[i & 511] = (char)('a' + (i & 15));
        if (what[3] == 'l') n += (long)strlen(big);
        else if (what[3] == 'c' && what[4] == 'm') n += strcmp(big, other) == 0;
        else n += (long)(strchr(big, 'y') - big);
    }
    return n;
}

static long heap(int mixed)
{
    void *live[1024];
    unsigned long state = 1;
    long i, n = 0;
    memset(live, 0, sizeof live);
    for (i = 0; i < 10000000L; i++) {
        unsigned slot = (unsigned)(next_random(&state) & 1023);
        size_t size = mixed ? (size_t)(16 + next_random(&state) % 4000) : (size_t)(16 + (next_random(&state) & 112));
        free(live[slot]);
        live[slot] = malloc(size);
        ((char *)live[slot])[0] = (char)i;
        n += (long)size;
    }
    for (i = 0; i < 1024; i++) free(live[i]);
    return n;
}

static long sorting(void)
{
    int *a = (int *)malloc(1000000 * sizeof(int));
    unsigned long state = 7;
    long i;
    for (i = 0; i < 1000000L; i++) a[i] = (int)next_random(&state);
    qsort(a, 1000000, sizeof(int), compare_ints);
    i = a[0] + a[999999];
    free(a);
    return i;
}

static long maths(const char *what)
{
    double sum = 0, x;
    long i;
    for (i = 0; i < 5000000L; i++) {
        x = 0.5 + (double)i * 1e-6;
        switch (what[1]) {
        case 'q': sum += sqrt(x); break;
        case 'i': sum += sin(x); break;
        case 'x': sum += exp(x); break;
        case 'o': if (what[2] == 'g') sum += log(x); else sum += pow(x, 1.37); break;
        }
    }
    return (long)sum;
}

static long scanning(int floats)
{
    char text[64];
    long i, n = 0;
    for (i = 0; i < 1000000L; i++) {
        if (floats) {
            double a;
            sprintf(text, "%ld.%03ld", i, i % 1000);
            sscanf(text, "%lf", &a);
            n += (long)a;
        } else {
            long a;
            sprintf(text, "%ld", i);
            sscanf(text, "%ld", &a);
            n += a;
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    int k;
    for (k = 0; k < (int)sizeof big; k++) big[k] = (char)(k * 31 + 7);
    if (argc < 2) {
        printf("hello\n");
        return 0;
    }
    for (k = 1; k < argc; k++) {
        const char *t = argv[k];
        clock_t start = clock();
        long check = 0;
        if (!strcmp(t, "printf-int")) check = printf_file("int");
        else if (!strcmp(t, "printf-float")) check = printf_file("float");
        else if (!strcmp(t, "printf-string")) check = printf_file("string");
        else if (!strcmp(t, "printf-stdout")) check = printf_stdout();
        else if (!strcmp(t, "fwrite")) check = make_file(0);
        else if (!strcmp(t, "fread")) { make_file(0); start = clock(); check = read_file(0); }
        else if (!strcmp(t, "fgets")) { make_file(1); start = clock(); check = read_file(1); }
        else if (!strcmp(t, "putc")) check = characters(0);
        else if (!strcmp(t, "getc")) { make_file(0); start = clock(); check = characters(1); }
        else if (!strncmp(t, "memcpy-", 7) || !strncmp(t, "memset-", 7) || !strncmp(t, "memmove-", 8))
            check = memory(t, atol(strchr(t, '-') + 1));
        else if (!strcmp(t, "strlen") || !strcmp(t, "strcmp") || !strcmp(t, "strchr")) check = strings(t);
        else if (!strcmp(t, "malloc-small")) check = heap(0);
        else if (!strcmp(t, "malloc-mixed")) check = heap(1);
        else if (!strcmp(t, "qsort")) check = sorting();
        else if (!strcmp(t, "sqrt") || !strcmp(t, "sin") || !strcmp(t, "exp") || !strcmp(t, "log") || !strcmp(t, "pow"))
            check = maths(t);
        else if (!strcmp(t, "sscanf-int")) check = scanning(0);
        else if (!strcmp(t, "sscanf-float")) check = scanning(1);
        else { fprintf(stderr, "no test %s\n", t); return 2; }
        sink += (unsigned long)check;
        fprintf(stderr, "%s %ld check %ld\n", t, (long)((clock() - start) * 1000L / CLOCKS_PER_SEC), check);
    }
    remove("bench-big.bin");
    return (int)(sink & 1);
}
