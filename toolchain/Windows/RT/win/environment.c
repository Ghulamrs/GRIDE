/* environment.c - the program's arguments and its environment, from Windows (c90).
 *
 * Spec: Microsoft C runtime - the command line parsed as its start-up parses it (the program name to
 * the first space outside quotes, its quotes dropped; then each argument: 2n backslashes before a
 * quote are n and the quote opens or closes a quoted part, 2n+1 are n and a quote; "" inside a quoted
 * part a quote; backslashes before anything else as they are), each argument then in the ANSI code
 * page, as main's argv has it; the environment read once into "NAME=value" strings, the "=C:" ones
 * left out, and getenv a search of them that ignores case - Windows' names do. */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "grt.h"
#include "win32.h"

static char *narrow(const WCHAR *wide, int count)
{
    int n = WideCharToMultiByte(GRT_CP_ACP, 0, wide, count, 0, 0, 0, 0);
    char *s = (char *)malloc((size_t)n + 1);
    if (!s) return 0;
    WideCharToMultiByte(GRT_CP_ACP, 0, wide, count, s, n, 0, 0);
    s[n] = 0;
    return s;
}

/* argc and argv from GetCommandLineW; argv[argc] is a null pointer */
void __grt_arguments(int *argc, char ***argv)
{
    const WCHAR *line = GetCommandLineW();
    size_t length = 0, cap = 8;
    int count = 0, quoted = 0;
    const WCHAR *p = line;
    WCHAR *word;
    char **vector = (char **)malloc(cap * sizeof(char *));
    while (line[length]) length++;
    word = (WCHAR *)malloc((length + 1) * sizeof(WCHAR));
    if (!vector || !word) { *argc = 0; *argv = vector; return; }

    /* the program's name: no backslash rule, a quote only toggles */
    {
        int n = 0;
        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') quoted = !quoted;
            else word[n++] = *p;
            p++;
        }
        vector[count++] = narrow(word, n);
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
            char **grown = (char **)realloc(vector, cap * 2 * sizeof(char *));
            if (!grown) break;
            vector = grown;
            cap *= 2;
        }
        vector[count++] = narrow(word, n);
    }
    vector[count] = 0;
    free(word);
    *argc = count;
    *argv = vector;
}

static char **environment;

char **__grt_environment(void)
{
    WCHAR *block, *s;
    int count = 0, k = 0;
    if (environment) return environment;
    block = GetEnvironmentStringsW();
    if (block)
        for (s = block; *s; ) { int n = 0; while (s[n]) n++; if (*s != '=') count++; s += n + 1; }
    environment = (char **)malloc(((size_t)count + 1) * sizeof(char *));
    if (!environment) return 0;
    if (block) {
        for (s = block; *s; ) {
            int n = 0;
            while (s[n]) n++;
            if (*s != '=' && k < count) environment[k++] = narrow(s, n);
            s += n + 1;
        }
        FreeEnvironmentStringsW(block);
    }
    environment[k] = 0;
    return environment;
}

static int same_letter(char a, char b)
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
        while (i < n && (*e)[i] && same_letter((*e)[i], name[i])) i++;
        if (i == n && (*e)[n] == '=') return *e + n + 1;
    }
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
