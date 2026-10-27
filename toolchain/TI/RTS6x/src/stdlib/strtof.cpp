// Spec: ISO C99 7.20.1.3 - strtof: strtod's subject sequence, correctly rounded to a float; ERANGE with
// HUGE_VALF or zero when it overflowed or underflowed.
#include <stdlib.h>
#include <errno.h>
#include "FloatText.h"
#include "../misc/ErrorNumber.h"
extern "C" float strtof(const char *s, char **end)
{
    const char *stop;
    bool range = false;
    unsigned long long bits = rts6x::FloatText::read(rts6x::FloatFormat::binary32(), s, &stop, range);
    if (range) rts6x::ErrorNumber::set(ERANGE);
    if (end) *end = (char *)stop;
    return rts6x::FloatBits::toFloat(bits);
}
