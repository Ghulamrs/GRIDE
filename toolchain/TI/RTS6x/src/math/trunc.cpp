// Spec: ISO C 7.12.9.8 - trunc, by its plain name. Not through <math.h>, which on the C6000 spells it
// __c6xabi_trunc (SPRAB89B Table 8-12); that helper is defined beside the others.

#include "IntegralPart.h"

extern "C" double trunc(double x)
{
    return rts6x::IntegralPart::towardZero(x);
}
