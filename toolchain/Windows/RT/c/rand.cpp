// Spec: ISO C 7.20.2 - rand's sequence in 0..RAND_MAX (32767), srand restarting it, rand before any srand as after
// srand(1); the sequence the Microsoft runtime gives - its generator (state * 214013 + 2531011, bits 16..30 of
// the state), one state for each thread - not RTS6x's, which is C's example generator (stdlib\Random.h).
#include <stdlib.h>

namespace {
thread_local unsigned randomState = 1;
}

extern "C" int rand(void)
{
    randomState = randomState * 214013u + 2531011u;
    return (int)((randomState >> 16) & 0x7FFF);
}

extern "C" void srand(unsigned int seed)
{
    randomState = seed;
}
