// Spec: ISO C 7.4.1.1 - isalnum, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int isalnum(int c)
{
    return rts6x::CharacterClass::alnum(c) ? 1 : 0;
}
