/* winmain.c - WinMainCRTStartup, where a program with WinMain begins (c90), in grtstart: WinMain(the image, a null
 * pointer, the command line past the program's name in the ANSI code page, the show command), and its answer given to
 * exit. The arguments are parsed as for main, for __argc and __argv. */
#include <stdlib.h>
#include "grt.h"

int WinMain(void *instance, void *previous, char *line, int show);
extern char __ImageBase[];

void WinMainCRTStartup(void)
{
    __grt_start(0);
    exit(__grt_call_entry((void *)WinMain, (long long)__ImageBase, 0, (long long)__grt_winmain_line(),
                          __grt_show_command()));
}
