/* main.c - mainCRTStartup, where a program with main begins (c90), in grtstart: main(argc, argv, envp) in the ANSI
 * code page, and its answer given to exit. */
#include <stdlib.h>
#include "grt.h"

int main(int argc, char **argv, char **envp);

void mainCRTStartup(void)
{
    __grt_start(0);
    exit(__grt_call_entry((void *)main, __argc, (long long)__argv, (long long)__grt_environment(), 0));
}
