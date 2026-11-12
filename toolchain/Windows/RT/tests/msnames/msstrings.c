/* msstrings.c - Microsoft's string names and their old (POSIX) spellings, as the UCRT answers them
   (docs\Runtime_Design.md 9.4, the policy of 2026-11-10): _strdup, _stricmp, _strnicmp, _strlwr, _strrev, _itoa,
   _snprintf and _vsnprintf, then strdup, stricmp, strnicmp, strlwr, strrev and itoa. The transcript is the UCRT's
   run (probe.ps1 -Record); the C6747's run of the same source prints the same (tests\review on sim6747). */
#define _POSIX_SOURCE 1
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static int sign(int v) { return v < 0 ? -1 : v > 0 ? 1 : 0; }

static void bytes(const char *what, const char *b, int n)
{
    int i;
    printf("%s:", what);
    for (i = 0; i < n; i++) {
        if (b[i] == 0) printf(" \\0");
        else printf(" %c", b[i]);
    }
    printf("\n");
}

static int vs(char *b, size_t n, const char *f, ...)
{
    va_list a;
    int r;
    va_start(a, f);
    r = _vsnprintf(b, n, f, a);
    va_end(a);
    return r;
}

int main(void)
{
    char buf[16], big[40];
    char *d;
    int r;

    d = _strdup("Hello, world");
    printf("_strdup: \"%s\" %s\n", d, d ? "fresh" : "null");
    free(d);
    printf("_strdup(\"\"): \"%s\"\n", (d = _strdup("")) ? d : "(null)");
    free(d);
    d = _strdup(NULL);
    printf("_strdup(NULL): %s\n", d ? "not null" : "null");

    printf("_stricmp: %d %d %d %d %d\n", sign(_stricmp("abc", "ABC")), sign(_stricmp("abc", "abd")),
           sign(_stricmp("ABD", "abc")), sign(_stricmp("ab", "abc")), sign(_stricmp("[", "a")));
    printf("_stricmp values: %d %d %d\n", _stricmp("a", "B"), _stricmp("Z", "a"), _stricmp("_", "A"));
    printf("_strnicmp: %d %d %d %d\n", sign(_strnicmp("abcX", "ABCy", 3)), sign(_strnicmp("abcX", "ABCy", 4)),
           _strnicmp("abc", "xyz", 0), sign(_strnicmp("ab", "AB\x7f", 5)));

    strcpy(buf, "MiXeD 123 @[`{");
    printf("_strlwr: \"%s\" same %d\n", _strlwr(buf), _strlwr(buf) == buf);
    strcpy(buf, "abcdef");
    printf("_strrev: \"%s\"", _strrev(buf));
    strcpy(buf, "abcde");
    printf(" \"%s\"", _strrev(buf));
    strcpy(buf, "");
    printf(" \"%s\"\n", _strrev(buf));

    printf("_itoa: %s", _itoa(0, big, 10));
    printf(" %s", _itoa(-12345, big, 10));
    printf(" %s", _itoa(-1, big, 16));
    printf(" %s", _itoa(255, big, 2));
    printf(" %s", _itoa(-2147483647 - 1, big, 10));
    printf(" %s", _itoa(-2147483647 - 1, big, 8));
    printf(" %s", _itoa(123456789, big, 36));
    printf(" %s\n", _itoa(-35, big, 36));

    memset(buf, '#', sizeof buf);
    r = _snprintf(buf, 8, "%d-%s", 42, "ab");
    printf("_snprintf fits: %d ", r);
    bytes("bytes", buf, 9);
    memset(buf, '#', sizeof buf);
    r = _snprintf(buf, 5, "%s", "abcde");
    printf("_snprintf exact: %d ", r);
    bytes("bytes", buf, 7);
    memset(buf, '#', sizeof buf);
    r = _snprintf(buf, 5, "%s", "abcdefgh");
    printf("_snprintf over: %d ", r);
    bytes("bytes", buf, 7);
    memset(buf, '#', sizeof buf);
    r = _snprintf(buf, 0, "%s", "x");
    printf("_snprintf none: %d ", r);
    bytes("bytes", buf, 2);
    r = _snprintf(buf, 0, "%s", "");
    printf("_snprintf empty: %d\n", r);
    memset(buf, '#', sizeof buf);
    r = vs(buf, 6, "%c%c%c%c%c%c%c", 'a', 'b', 'c', 'd', 'e', 'f', 'g');
    printf("_vsnprintf over: %d ", r);
    bytes("bytes", buf, 8);
    memset(buf, '#', sizeof buf);
    r = vs(buf, 6, "%.2f", 3.14159);
    printf("_vsnprintf fits: %d ", r);
    bytes("bytes", buf, 6);

    d = strdup("plain");
    printf("strdup: %s\n", d);
    free(d);
    printf("stricmp: %d strnicmp: %d\n", sign(stricmp("Plain", "PLAIN")), sign(strnicmp("Plain", "PLAZA", 3)));
    strcpy(buf, "OLD Names");
    printf("strlwr: %s ", strlwr(buf));
    printf("strrev: %s ", strrev(buf));
    printf("itoa: %s\n", itoa(-77, big, 10));
    return 0;
}
