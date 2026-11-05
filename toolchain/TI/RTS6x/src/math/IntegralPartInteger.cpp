// Spec: ISO C99 7.12.9.5 lrint, llrint and 7.12.9.7 lround, llround, as the Microsoft runtime gives them (tests\math99):
// a NaN, an infinity or a value outside the type is 0 and EDOM, and 2^63 itself is LLONG_MIN. That one is named here,
// since a conversion's answer past the range is each target's own (the C6000 ABI's saturates).

#include <errno.h>
#include <limits.h>
#include "IntegralPart.h"
#include "MathError.h"

namespace rts6x {

long IntegralPart::toLong(double r)
{
    if (r != r || r > 2147483647.0 || r < -2147483648.0) {
        ErrorNumber::set(EDOM);
        return 0;
    }
    return (long)r;
}

long long IntegralPart::toLongLong(double r)
{
    if (r != r || r > 9223372036854775808.0 || r < -9223372036854775808.0) {
        ErrorNumber::set(EDOM);
        return 0;
    }
    if (r == 9223372036854775808.0) return LLONG_MIN;
    return (long long)r;
}

}  // namespace rts6x
