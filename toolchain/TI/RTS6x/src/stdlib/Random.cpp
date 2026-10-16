// Spec: ISO C 7.20.2.2/2 - with no srand, the sequence is the one srand(1) starts.

#include "Random.h"

unsigned rts6x::Random::state_ = 1;
