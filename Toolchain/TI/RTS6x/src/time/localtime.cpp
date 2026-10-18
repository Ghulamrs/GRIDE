// Spec: ISO C 7.23.3.4 - localtime: t as local time. The C6747 has no time zone, so local time is UTC.

#include "Calendar.h"

extern "C" struct tm *localtime(const time_t *t)
{
    static struct tm result;
    rts6x::Calendar::breakDown(*t, &result);
    return &result;
}
