// args.cpp - what a program is given when it starts, for utf8.ps1: argv, the environment and the names Windows hands
// back, in a folder with a name past the ANSI code page, and a file named on the command line.
//
//   args utf8 <file> | early <file> | plain <file>
//
// utf8 asks for UTF-8 at the top of main, early in a static object's constructor, before argv is made (the variable
// GRT_PROBE_EARLY says so, the arguments not being there to read), and plain not at all.
#include <errno.h>
#include <io.h>
#include <direct.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int early = getenv("GRT_PROBE_EARLY") ? (setlocale(LC_CTYPE, ".UTF-8"), 1) : 0;

int main(int argc, char **argv, char **envp)
{
    const char *taken = getenv("GRT_PROBE");
    const char *spelled = argc > 2 ? argv[2] : "";
    size_t before = strlen(spelled);
    if (argc > 1 && strcmp(argv[1], "utf8") == 0) setlocale(LC_CTYPE, ".UTF-8");
    printf("argc %d early %d\n", argc, early);
    if (argc < 3) return 2;
    printf("argv %s\n", argv[2]);
    printf("the string taken first %u bytes, still %u\n", (unsigned)before, (unsigned)strlen(spelled));
    errno = 0;
    FILE *f = fopen(argv[2], "r");
    printf("fopen %d errno %d\n", f != 0, errno);
    if (f) {
        char line[64] = "";
        if (fgets(line, sizeof line, f)) printf("holds %s", line);
        fclose(f);
    }
    printf("getenv %s\n", getenv("GRT_PROBE"));
    printf("getenv taken first %u bytes\n", taken ? (unsigned)strlen(taken) : 0u);
    for (; *envp; envp++)
        if (strncmp(*envp, "GRT_PROBE=", 10) == 0) printf("envp %s\n", *envp);
    char here[300];
    if (_getcwd(here, sizeof here)) {
        const char *last = strrchr(here, '\\');
        printf("getcwd %s\n", last ? last + 1 : here);
    }
    struct _finddata64i32_t found;
    long long h = _findfirst64i32("*.txt", &found);
    if (h != -1) {
        do printf("found %s\n", found.name);
        while (_findnext64i32(h, &found) == 0);
        _findclose(h);
    }
    return 0;
}
