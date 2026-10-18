// Spec: ISO C 7.19.7.6 - getchar is getc on stdin.

#include "Stream.h"

extern "C" int getchar(void)
{
    return rts6x::Stream(stdin).get();
}
