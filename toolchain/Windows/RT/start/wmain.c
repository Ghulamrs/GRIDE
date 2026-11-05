/* wmain.c - wmainCRTStartup, where a program with wmain begins (c90), in grtstart: wmain(argc, argv, envp) in UTF-16,
 * and its answer given to exit. */
#include <stdlib.h>
#include "grt.h"

int wmain(int argc, wchar_t **argv, wchar_t **envp);

void wmainCRTStartup(void)
{
    __grt_start(1);
    exit(__grt_call_entry((void *)wmain, __argc, (long long)__wargv, (long long)__grt_wide_environment(), 0));
}
