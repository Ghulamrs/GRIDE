// Spec: SPRAB89B 8.2 Table 8-6, uint64 x / y - __c6xabi_divull.

#include "WideDivision.h"

extern "C" unsigned long long __c6xabi_divull(unsigned long long x, unsigned long long y)
{
    unsigned long long remainder;
    return rts6x::WideDivision::divide(x, y, remainder);
}
