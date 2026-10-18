// Spec: ISO C 7.19.5.2 - fflush delivers what a stream holds; RTS6x's streams hold nothing yet
// (every write reaches the host before it returns), so there is nothing to deliver: 0.

#include <stdio.h>

extern "C" int fflush(FILE *stream)
{
    (void)stream;
    return 0;
}
