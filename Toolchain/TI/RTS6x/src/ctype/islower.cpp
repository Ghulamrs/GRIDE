// Spec: ISO C 7.4.1.7 - islower, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int islower(int c)
{
    return rts6x::CharacterClass::lower(c) ? 1 : 0;
}
