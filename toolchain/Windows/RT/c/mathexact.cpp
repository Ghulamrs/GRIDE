// Spec: ISO C99 7.12.11.4 nexttoward - nextafter (RTS6x's) toward a long double, which on x64 is a double, as the
// Microsoft runtime gives it (tests\math99). The rest of <math.h>'s functions on a double's bits are RTS6x's.
#include <math.h>

extern "C" double nexttoward(double x, long double y)
{
    return nextafter(x, (double)y);
}
