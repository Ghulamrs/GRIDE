// Spec: ISO C 7.4.1.5 - isdigit, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int isdigit(int c)
{
    return rts6x::CharacterClass::digit(c) ? 1 : 0;
}
