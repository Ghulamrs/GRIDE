/* chartype.c - a UTF-16 code unit's classes for wctype (c\wctype.cpp), as Windows keeps them (c90): GetStringTypeW's
 * CT_CTYPE1, whose bits are the Microsoft runtime's ctype masks. */
#include "grt.h"
#include "win32.h"

unsigned short __grt_char_type(unsigned short c)
{
    WCHAR w = c;
    unsigned short t = 0;
    if (!GetStringTypeW(GRT_CT_CTYPE1, &w, 1, &t)) return 0;
    return t;
}
