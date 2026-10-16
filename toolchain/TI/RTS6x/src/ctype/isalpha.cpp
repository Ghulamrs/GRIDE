// Spec: ISO C 7.4.1.2 - isalpha, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int isalpha(int c)
{
    return rts6x::CharacterClass::alpha(c) ? 1 : 0;
}
