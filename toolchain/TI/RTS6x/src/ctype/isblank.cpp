// Spec: ISO C99 7.4.1.3 - isblank, in the "C" locale a space or a tab, answered as the Microsoft runtime's table answers
// (_BLANK, 0x40), on the C6747 too, so that the number a program prints is the same on every target.

#include <ctype.h>
#include "CharacterClass.h"

extern "C" int isblank(int c)
{
    return rts6x::CharacterClass::blank(c) ? 0x40 : 0;
}
