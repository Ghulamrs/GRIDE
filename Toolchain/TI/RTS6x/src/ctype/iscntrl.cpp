// Spec: ISO C 7.4.1.4 - iscntrl, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int iscntrl(int c)
{
    return rts6x::CharacterClass::control(c) ? 1 : 0;
}
