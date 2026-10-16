// Spec: ISO C 7.4.1.6 - isgraph, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int isgraph(int c)
{
    return rts6x::CharacterClass::graph(c) ? 1 : 0;
}
