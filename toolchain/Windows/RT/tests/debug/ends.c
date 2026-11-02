/* ends.c <abort|overrun> - the Debug runtime's two ends that Microsoft's Debug runtime shows a dialog for: abort, and a
 * free that finds a block's guard overwritten. Run by debug.ps1 against GRIDE's Debug runtime alone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    fputs("start\n", stderr);
    if (argc > 1 && !strcmp(argv[1], "abort")) abort();
    if (argc > 1 && !strcmp(argv[1], "overrun")) {
        char *p = (char *)malloc(8);
        p[8] = 'x';
        free(p);
    }
    fputs("not reached\n", stderr);
    return 0;
}
