// Spec: ISO C 7.4.1.10 - isspace, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int isspace(int c)
{
    return rts6x::CharacterClass::space(c) ? 1 : 0;
}
