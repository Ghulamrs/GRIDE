// Spec: ISO C 7.4.2.2 - toupper, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int toupper(int c)
{
    return rts6x::CharacterClass::toUpper(c);
}
