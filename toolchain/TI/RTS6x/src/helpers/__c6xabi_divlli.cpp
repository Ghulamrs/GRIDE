// Spec: SPRAB89B 8.2 Table 8-6, int64 x / y - __c6xabi_divlli.

#include "WideDivision.h"

extern "C" long long __c6xabi_divlli(long long x, long long y)
{
    long long remainder;
    return rts6x::WideDivision::divide(x, y, remainder);
}
