// Spec: ISO C 7.4.1.11 - isupper, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int isupper(int c)
{
    return rts6x::CharacterClass::upper(c) ? 1 : 0;
}
