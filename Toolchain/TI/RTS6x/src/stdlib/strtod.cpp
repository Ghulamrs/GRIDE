// Spec: ISO C 7.20.1.3 - strtod: the value, correctly rounded; the end where the number stopped, or
// the start if there was none; ERANGE with HUGE_VAL or zero when it overflowed or underflowed.

#include <stdlib.h>
#include <errno.h>
#include "FloatText.h"
#include "../misc/ErrorNumber.h"

extern "C" double strtod(const char *s, char **end)
{
    const char *stop;
    bool range = false;
    unsigned long long bits;
    if (!rts6x::FloatText::quick(s, &stop, bits))
        bits = rts6x::FloatText::read(rts6x::FloatFormat::binary64(), s, &stop, range);
    if (range) rts6x::ErrorNumber::set(ERANGE);
    if (end) *end = (char *)stop;
    return rts6x::FloatBits::toDouble(bits);
}
