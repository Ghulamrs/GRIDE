// Spec: ISO C 7.4.1.9 - ispunct, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int ispunct(int c)
{
    return rts6x::CharacterClass::punct(c) ? 1 : 0;
}
