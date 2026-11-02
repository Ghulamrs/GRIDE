/* printf.c - the printf family's probe (docs\Runtime_Design.md 10.3 and 11.2): formats as the Microsoft runtime
 * reads them, to a stream and into memory, each line with what the call answered, so that the same object linked
 * with GRIDE's runtime and with the UCRT prints the same lines (tests\probe.ps1 printf\printf.cpp). The calls that
 * fail fast (%n, two length modifiers, N, L on an integer) are left out: they end the program. */
#include <stdio.h>
#include <string.h>
#include <errno.h>

static char buf[1024];

#define SHOW(call) do { int r_; errno = 0; memset(buf, '#', sizeof buf); r_ = call; \
    printf("| %d errno %d [%s]\n", r_, errno, buf); fflush(stdout); } while (0)

static const unsigned short wide[] = { 'w', 'i', 'd', 'e', 0 };
static const unsigned short wideHigh[] = { 'o', 'k', 0x100, 'x', 0 };
static const unsigned short wideFF[] = { 0xE9, 0xFF, 'a', 0 };

struct Counted { unsigned short length, maximum; const char *buffer; };

int main(void)
{
    static const char *texts[] = {
        "plain text, no conversion", "%%", "100%% sure", "%y", "%5.3.2f", "%k%m%q", "%-5%|", "% 5%|",
        "%Iz", "%I6d", "%I3d", "%.", "%5", "%-", "tail %"
    };
    static const int ints[] = { 0, 1, -1, 42, -42, 2147483647, -2147483647 - 1, 1000000, -9 };
    static const char *intFormats[] = {
        "%d", "%5d", "%-5d|", "%05d", "%+d", "% d", "%+05d", "% 05d", "%.0d", "%.3d", "%8.3d", "%-8.3d|", "%08.3d",
        "%i", "%u", "%o", "%#o", "%#.0o", "%x", "%#x", "%#X", "%#08x", "%#-8x|", "%hd", "%hhd", "%hu", "%hhu",
        "%ld", "%lu", "%#5.3x", "%-+8d|", "%+.0d", "% .0d", "%I32d", "%jd", "%zd", "%td", "%Id", "%I64d", "%lld"
    };
    static const long long bigs[] = { 0, 1, -1, 9223372036854775807LL, -9223372036854775807LL - 1, 4294967296LL,
                                      -4294967296LL, 123456789012345LL };
    static const char *bigFormats[] = { "%lld", "%llu", "%llx", "%#llo", "%I64d", "%I64X", "%jd", "%zu", "%tx",
                                        "%Id", "%+25lld|", "%-25lld|", "%025lld", "%.20lld", "%Ix" };
    static const double doubles[] = { 0.0, -0.0, 1.0, -1.5, 3.14159265358979, 1e-7, 123456789.125, 1e300, -2.5e-300,
                                      0.1, 1.0 / 3.0, 65536.0 };
    static const char *floatFormats[] = {
        "%f", "%e", "%g", "%a", "%F", "%E", "%G", "%A", "%.0f", "%#.0f", "%.0e", "%#.0e", "%.0g", "%#.0g", "%#g",
        "%12.4f|", "%-12.4f|", "%012.4f", "%+f", "% f", "%+012.3e", "%-+12.3e|", "%.3a", "%020a", "%-20a|", "%.15g",
        "%.17g", "%10.0f|", "%#10.3g|", "%.30f", "%lf", "%Lf", "%hf", "%.1000e"
    };
    unsigned i, k;
    double inf = 1e308 * 10.0, nan = inf - inf, negInf = -inf;
    struct Counted counted = { 7, 8, "counted" };
    struct Counted none = { 0, 0, 0 };

    for (i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        int r = printf(texts[i]);
        printf("| %d\n", r);
        SHOW(sprintf(buf, texts[i]));
    }
    for (i = 0; i < sizeof intFormats / sizeof intFormats[0]; i++)
        for (k = 0; k < sizeof ints / sizeof ints[0]; k++) {
            printf("%-10s %11d: ", intFormats[i], ints[k]);
            SHOW(sprintf(buf, intFormats[i], ints[k]));
        }
    for (i = 0; i < sizeof bigFormats / sizeof bigFormats[0]; i++)
        for (k = 0; k < sizeof bigs / sizeof bigs[0]; k++) {
            printf("%-10s %21lld: ", bigFormats[i], bigs[k]);
            SHOW(sprintf(buf, bigFormats[i], bigs[k]));
        }
    for (i = 0; i < sizeof floatFormats / sizeof floatFormats[0]; i++) {
        for (k = 0; k < sizeof doubles / sizeof doubles[0]; k++) {
            printf("%-10s %d: ", floatFormats[i], (int)k);
            SHOW(snprintf(buf, 300, floatFormats[i], doubles[k]));
        }
        printf("%-10s inf/nan: ", floatFormats[i]);
        SHOW(sprintf(buf, "%s %s %s", "", "", ""));
        SHOW(snprintf(buf, 300, floatFormats[i], inf));
        SHOW(snprintf(buf, 300, floatFormats[i], negInf));
        SHOW(snprintf(buf, 300, floatFormats[i], nan));
        SHOW(snprintf(buf, 300, floatFormats[i], -nan));
    }

    /* strings and characters */
    SHOW(sprintf(buf, "[%s] [%10s] [%-10s] [%.2s] [%010s] [%-010s] [%10.3s]", "abc", "abc", "abc", "abc", "abc", "abc", "abcdef"));
    SHOW(sprintf(buf, "[%s] [%10s] [%.3s] [%010s]", (char *)0, (char *)0, (char *)0, (char *)0));
    SHOW(sprintf(buf, "[%c] [%5c] [%-5c] [%05c] [%c] [%hc] [%c]", 'x', 'y', 'z', 'w', 0x141, 0x2242, 0));
    SHOW(sprintf(buf, "[%C] [%lc] [%wc] [%5C] [%05lc]", 'A', 'B', 'C', 'D', 'E'));
    SHOW(sprintf(buf, "[%C] after", 0x100));
    SHOW(sprintf(buf, "[%5lc] after", 0x263A));
    SHOW(sprintf(buf, "[%S] [%ls] [%ws] [%10S] [%-10ls] [%.2S] [%010S]", wide, wide, wide, wide, wide, wide, wide));
    SHOW(sprintf(buf, "[%S] [%ls]", (unsigned short *)0, (unsigned short *)0));
    SHOW(sprintf(buf, "[%S] [%ls]", wideFF, wideFF));
    SHOW(sprintf(buf, "[%hs] [%hS] [%hc] [%hC]", "narrow", "narrow", 'n', 'N'));
    SHOW(sprintf(buf, "[%Z] [%Z] [%hZ]", &counted, &none, &counted));
    SHOW(sprintf(buf, "before [%S] after", wideHigh));
    SHOW(snprintf(buf, 50, "before [%ls] after", wideHigh));
    {
        int r = printf("stream before [%S] after\n", wideHigh);
        printf("| %d\n", r);
    }

    /* pointers */
    SHOW(sprintf(buf, "[%p] [%p] [%20p] [%-20p] [%020p] [%#p] [%.4p] [%hp] [%lp]", (void *)0, (void *)0x7FF612345678,
                 (void *)0x1234, (void *)0x1234, (void *)0x1234, (void *)0x1234, (void *)0x1234, (void *)0x1234, (void *)0x1234));

    /* widths and precisions from the arguments */
    SHOW(sprintf(buf, "[%*d] [%-*d] [%*d] [%.*d] [%.*d] [%*.*f] [%*s]", 6, 42, 6, 42, -6, 42, 4, 42, -4, 42, 10, 3, 3.14159, 5, "ab"));
    SHOW(sprintf(buf, "[%*05d] [%0*d] [%-*.*s]", 7, 42, 7, 42, 8, 2, "abcdef"));
    SHOW(snprintf(buf, 40, "[%99d]", 1));
    SHOW(sprintf(buf, "[%2147483648d] after", 1));
    SHOW(sprintf(buf, "[%.2147483648d] after", 1));

    /* snprintf's bounds */
    for (k = 0; k <= 12; k++) {
        memset(buf, '#', sizeof buf);
        i = (unsigned)snprintf(buf, k, "%s-%d", "twelve", 12345);
        printf("snprintf %2u: %d [%.20s]\n", k, (int)i, buf);
    }
    printf("snprintf(NULL, 0): %d\n", snprintf((char *)0, 0, "%d %s", 123456, "counted"));

    /* to a file and back */
    {
        FILE *f = fopen("printf.tmp", "w");
        int r = fprintf(f, "%s %05d %-6.2f|%c\n", "line", 42, 3.5, 'z');
        int back;
        printf("fprintf: %d\n", r);
        r = fprintf(f, "second [%10s] %x\n", "line", 255);
        printf("fprintf: %d\n", r);
        fclose(f);
        f = fopen("printf.tmp", "rb");
        back = (int)fread(buf, 1, sizeof buf - 1, f);
        buf[back > 0 ? back : 0] = 0;
        fclose(f);
        remove("printf.tmp");
        for (k = 0; buf[k]; k++) printf(buf[k] == '\r' ? "<CR>" : buf[k] == '\n' ? "<LF>\n" : "%c", buf[k]);
    }
    return 0;
}
