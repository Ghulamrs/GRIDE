// Spec: ISO C 7.23.2.3 - mktime: the time tm names, its fields normalised; -1 if it has none.

#include "Calendar.h"

extern "C" time_t mktime(struct tm *tm)
{
    return rts6x::Calendar::compose(tm);
}
