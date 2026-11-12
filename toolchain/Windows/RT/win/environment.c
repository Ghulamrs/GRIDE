/* environment.c - the program's arguments and its environment, from Windows (c90).
 *
 * Spec: Microsoft C runtime - the command line parsed as its start-up parses it (the program name to
 * the first space outside quotes, its quotes dropped; then each argument: 2n backslashes before a
 * quote are n and the quote opens or closes a quoted part, 2n+1 are n and a quote; "" inside a quoted
 * part a quote; backslashes before anything else as they are), an empty command line taken as the
 * program's path, each argument then in the ANSI code page as main's argv has it, or in UTF-16 as
 * wmain's has it; the environment read once into "NAME=value" strings, the "=C:" ones left out, and
 * getenv a search of them that ignores case - Windows' names do. Once setlocale asks for UTF-8 the narrow
 * ones are spelled again in it (win\utf8.c). */
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "grt.h"
#include "win32.h"

static int arguments;
static char **narrowArguments;
static WCHAR **wideArguments;

int *__p___argc(void) { return &arguments; }
char ***__p___argv(void) { return &narrowArguments; }
WCHAR ***__p___wargv(void) { return &wideArguments; }

static void *grt_narrow(const WCHAR *wide, int count)
{
    int n = WideCharToMultiByte(GRT_CP_NARROW, 0, wide, count, 0, 0, 0, 0);
    char *s = (char *)malloc((size_t)n + 1);
    if (!s) return 0;
    WideCharToMultiByte(GRT_CP_NARROW, 0, wide, count, s, n, 0, 0);
    s[n] = 0;
    return s;
}

static void *grt_wide(const WCHAR *wide, int count)
{
    WCHAR *s = (WCHAR *)malloc(((size_t)count + 1) * sizeof(WCHAR));
    int i;
    if (!s) return 0;
    for (i = 0; i < count; i++) s[i] = wide[i];
    s[count] = 0;
    return s;
}

/* the arguments from GetCommandLineW; the vector's last element is a null pointer */
void __grt_arguments(int wide)
{
    static WCHAR program[261];
    void *(*keep)(const WCHAR *, int) = wide ? grt_wide : grt_narrow;
    const WCHAR *line = GetCommandLineW();
    size_t length = 0, cap = 8;
    int count = 0, quoted = 0;
    const WCHAR *p;
    WCHAR *word;
    void **vector = (void **)malloc(cap * sizeof(void *));
    if (!*line && GetModuleFileNameW(0, program, 260)) line = program;
    p = line;
    while (line[length]) length++;
    word = (WCHAR *)malloc((length + 1) * sizeof(WCHAR));
    if (!vector || !word) return;

    /* the program's name: no backslash rule, a quote only toggles */
    {
        int n = 0;
        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') quoted = !quoted;
            else word[n++] = *p;
            p++;
        }
        vector[count++] = keep(word, n);
    }
    for (;;) {
        int n = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        quoted = 0;
        for (;;) {
            unsigned slashes = 0;
            int copy = 1;
            while (*p == '\\') { p++; slashes++; }
            if (*p == '"') {
                if (slashes % 2 == 0) {
                    if (quoted && p[1] == '"') p++;
                    else { copy = 0; quoted = !quoted; }
                }
                slashes /= 2;
            }
            while (slashes-- > 0) word[n++] = '\\';
            if (!*p || (!quoted && (*p == ' ' || *p == '\t'))) break;
            if (copy) word[n++] = *p;
            p++;
        }
        if ((size_t)count + 1 >= cap) {
            void **grown = (void **)realloc(vector, cap * 2 * sizeof(void *));
            if (!grown) break;
            vector = grown;
            cap *= 2;
        }
        vector[count++] = keep(word, n);
    }
    vector[count] = 0;
    free(word);
    arguments = count;
    if (wide) wideArguments = (WCHAR **)vector;
    else narrowArguments = (char **)vector;
}

static void **grt_read_environment(void *(*keep)(const WCHAR *, int))
{
    void **environment;
    WCHAR *block, *s;
    int count = 0, k = 0;
    block = GetEnvironmentStringsW();
    if (block)
        for (s = block; *s; ) { int n = 0; while (s[n]) n++; if (*s != '=') count++; s += n + 1; }
    environment = (void **)malloc(((size_t)count + 1) * sizeof(void *));
    if (!environment) return 0;
    if (block) {
        for (s = block; *s; ) {
            int n = 0;
            while (s[n]) n++;
            if (*s != '=' && k < count) environment[k++] = keep(s, n);
            s += n + 1;
        }
        FreeEnvironmentStringsW(block);
    }
    environment[k] = 0;
    return environment;
}

char **__grt_envp_given;

char **__grt_environment(void)
{
    if (!__grt_envp_given) __grt_envp_given = (char **)grt_read_environment(grt_narrow);
    return __grt_envp_given;
}

static WCHAR **wideEnvironment;

WCHAR **__grt_wide_environment(void)
{
    if (!wideEnvironment) wideEnvironment = (WCHAR **)grt_read_environment(grt_wide);
    return wideEnvironment;
}

static int grt_same_letter(char a, char b)
{
    if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
    if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
    return a == b;
}

/* Spec: ISO C 7.20.4.5 - getenv: the value of name, in a string the program must not change; a null
   pointer when it has none. The string lives as long as the program. */
char *getenv(const char *name)
{
    char **e = __grt_environment();
    size_t n = 0;
    if (!e || !name || !*name) return 0;
    for (; name[n]; n++)                    /* the lowest layer counts for itself: no strlen, no strchr */
        if (name[n] == '=') return 0;
    for (; *e; e++) {
        size_t i = 0;
        while (i < n && (*e)[i] && grt_same_letter((*e)[i], name[i])) i++;
        if (i == n && (*e)[n] == '=') return *e + n + 1;
    }
    return 0;
}

/* Spec: Microsoft C runtime, _putenv - "NAME=value" sets NAME and "NAME=" removes it, in the table getenv reads and
   in Windows' own environment, which a program system() starts inherits; the wide table is read again from
   Windows when next asked for. 0, or -1 with errno: EINVAL for an entry with no name or no '=', ENOMEM. A null
   entry fails fast, as the UCRT's invalid parameter does. The replaced strings are not freed: a pointer getenv gave
   for them may still be held. */
int _putenv(const char *entry)
{
    char **e, **grown, *copy;
    size_t n = 0, len, k, count, at;
    WCHAR *wide, *w;
    if (!entry) __grt_fail_fast();
    while (entry[n] && entry[n] != '=') n++;                /* the lowest layer counts for itself */
    if (n == 0 || entry[n] != '=') { errno = EINVAL; return -1; }
    for (len = n; entry[len]; len++) {}
    e = __grt_environment();
    if (!e) { errno = ENOMEM; return -1; }
    wide = __grt_widen(entry);
    if (!wide) return -1;
    for (w = wide; *w && *w != (WCHAR)'='; w++) {}
    *w = 0;
    SetEnvironmentVariableW(wide, w[1] ? w + 1 : 0);
    HeapFree(GetProcessHeap(), 0, wide);
    wideEnvironment = 0;
    for (count = 0; e[count]; count++) {}
    for (at = 0; at < count; at++) {
        k = 0;
        while (k < n && e[at][k] && grt_same_letter(e[at][k], entry[k])) k++;
        if (k == n && e[at][n] == '=') break;
    }
    if (entry[n + 1] == 0) {                                /* "NAME=": removed */
        if (at < count) for (; at < count; at++) e[at] = e[at + 1];
        return 0;
    }
    copy = (char *)malloc(len + 1);
    if (!copy) { errno = ENOMEM; return -1; }
    for (k = 0; k <= len; k++) copy[k] = entry[k];
    if (at < count) { e[at] = copy; return 0; }
    grown = (char **)realloc(e, (count + 2) * sizeof(char *));
    if (!grown) { free(copy); errno = ENOMEM; return -1; }
    grown[count] = copy;
    grown[count + 1] = 0;
    __grt_envp_given = grown;
    return 0;
}

int __grt_getenv(const char *name, char *value, unsigned capacity)
{
    const char *v = getenv(name);
    unsigned i;
    if (!v || capacity == 0) return 0;
    for (i = 0; i + 1 < capacity && v[i]; i++) value[i] = v[i];
    value[i] = 0;
    return 1;
}
