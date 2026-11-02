// Spec: ISO C 7.19.4.4 - tmpnam: a name no file has, into s (at least L_tmpnam bytes) or, for a null s, into a
// buffer a later call may overwrite - one for each thread, as the Microsoft runtime keeps it; the name formed as
// that runtime forms it (win\temporary.c). A null pointer when no name can be made.
#include <stdio.h>
#include "grt.h"

namespace {
thread_local char tmpnamText[L_tmpnam];
}

extern "C" char *tmpnam(char *s)
{
    char *out = s ? s : tmpnamText;
    return __grt_temporary_name('s', out, L_tmpnam) == 0 ? out : 0;
}
