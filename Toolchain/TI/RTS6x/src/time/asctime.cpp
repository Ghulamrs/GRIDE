// Spec: ISO C 7.23.3.1 - asctime: "Sun Sep 16 01:03:52 1973\n" for tm, the form the standard gives.

#include "TimeFormatter.h"

extern "C" char *asctime(const struct tm *tm)
{
    static char result[32];
    rts6x::TimeFormatter f(result, sizeof result);
    f.run("%a %b %e %H:%M:%S %Y\n", tm);
    return result;
}
