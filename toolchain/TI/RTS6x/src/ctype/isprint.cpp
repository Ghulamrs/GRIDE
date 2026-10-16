// Spec: ISO C 7.4.1.8 - isprint, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int isprint(int c)
{
    return rts6x::CharacterClass::print(c) ? 1 : 0;
}
