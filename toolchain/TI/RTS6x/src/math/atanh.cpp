// Spec: ISO C99 7.12.5.3 atanh and F.9.2.3, with the errno the Microsoft runtime's UCRT sets (tests\math99): past 1 a
// domain error, at 1 a pole. ln((1 + a) / (1 - a)) / 2 in double-double arithmetic, rounded once.
#include <math.h>
#include "Logarithm.h"
#include "MathError.h"
using rts6x::DoubleDouble;
using rts6x::MathError;
extern "C" double atanh(double x)
{
    if (x != x) return x + x;
    double a = fabs(x);
    if (a > 1.0) return MathError::domainC99();
    if (a == 1.0) return MathError::overflow(x < 0);
    if (a < 3.725290298461914e-09) return x;
    DoubleDouble q = DoubleDouble::sum(1.0, a).over(DoubleDouble::sum(1.0, -a));
    double r = 0.5 * rts6x::Logarithm::extended(q).value();
    return x < 0 ? -r : r;
}
