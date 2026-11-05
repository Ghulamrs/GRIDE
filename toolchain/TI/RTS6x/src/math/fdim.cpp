// Spec: ISO C99 7.12.12.1 fdim and F.9.9.1, as the Microsoft runtime gives it (tests\math99): no errno for an overflow.
#include <math.h>
extern "C" double fdim(double x, double y)
{
    if (x != x) return x;
    if (y != y) return y;
    return x > y ? x - y : 0.0;
}
