/* wwinmain.c - wWinMainCRTStartup, where a program with wWinMain begins (c90), in grtstart: wWinMain(the image, a null
 * pointer, the command line past the program's name in UTF-16, the show command), and its answer given to exit. The
 * arguments are parsed as for wmain, for __argc and __wargv. */
#include <stdlib.h>
#include "grt.h"

int wWinMain(void *instance, void *previous, wchar_t *line, int show);
extern char __ImageBase[];

void wWinMainCRTStartup(void)
{
    __grt_start(1);
    exit(__grt_call_entry((void *)wWinMain, (long long)__ImageBase, 0, (long long)__grt_wide_winmain_line(),
                          __grt_show_command()));
}
