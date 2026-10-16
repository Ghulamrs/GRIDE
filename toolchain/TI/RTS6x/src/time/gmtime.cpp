// Spec: ISO C 7.23.3.3 - gmtime: t broken down as UTC, in a struct a later call may overwrite.

#include "Calendar.h"

extern "C" struct tm *gmtime(const time_t *t)
{
    static struct tm result;
    rts6x::Calendar::breakDown(*t, &result);
    return &result;
}
