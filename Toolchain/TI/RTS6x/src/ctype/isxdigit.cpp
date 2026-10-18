// Spec: ISO C 7.4.1.12 - isxdigit, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int isxdigit(int c)
{
    return rts6x::CharacterClass::xdigit(c) ? 1 : 0;
}
