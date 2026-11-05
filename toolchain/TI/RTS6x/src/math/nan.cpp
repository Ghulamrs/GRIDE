// Spec: ISO C99 7.12.11.2 nan, as the Microsoft runtime gives it (tests\math99): the default quiet NaN whatever its text.
#include <math.h>
#include "MathBits.h"
extern "C" double nan(const char *)
{
    return rts6x::MathBits::notANumber();
}
