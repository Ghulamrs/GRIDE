// Spec: ISO C 7.4.2.1 - tolower, in the "C" locale.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int tolower(int c)
{
    return rts6x::CharacterClass::toLower(c);
}
