// Spec: ISO C 7.20.2.2 - srand.

#include <stdlib.h>
#include "Random.h"

extern "C" void srand(unsigned int seed)
{
    rts6x::Random::seed(seed);
}
