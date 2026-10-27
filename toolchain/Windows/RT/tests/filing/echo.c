/* echo.c - the command processor filing.ps1 gives the probe's system: its program name (no folder) and its
 * arguments, each in brackets on one line, and the last argument as its exit code. */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *name = argv[0], *p;
    int i;
    for (p = argv[0]; *p; p++) if (*p == '\\' || *p == '/') name = p + 1;
    printf("  [%s]", name);
    for (i = 1; i < argc; i++) printf(" [%s]", argv[i]);
    printf("\n");
    return argc > 1 ? atoi(argv[argc - 1]) : 0;
}
