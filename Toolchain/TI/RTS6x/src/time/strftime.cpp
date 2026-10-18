// Spec: ISO C 7.23.3.5 - strftime: the format with tm's fields in it, at most max bytes with the NUL;
// the count without the NUL, or 0 if it did not fit.

#include "TimeFormatter.h"

extern "C" size_t strftime(char *s, size_t max, const char *format, const struct tm *tm)
{
    return rts6x::TimeFormatter(s, max).run(format, tm);
}
