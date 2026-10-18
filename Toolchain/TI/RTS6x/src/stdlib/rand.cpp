// Spec: ISO C 7.20.2.1 - rand.

#include <stdlib.h>
#include "Random.h"

extern "C" int rand(void)
{
    return rts6x::Random::next();
}
