// Spec: ISO C 7.25 (wctype.h) as the Microsoft runtime has it (tests\wide\wide.expected):
// - A code unit's classes are Windows' CT_CTYPE1 (GetStringTypeW, through grtwin), whose bits are the runtime's own
//   masks: _UPPER 1, _LOWER 2, _DIGIT 4, _SPACE 8, _PUNCT 0x10, _CONTROL 0x20, _BLANK 0x40, _HEX 0x80, _ALPHA 0x100.
//   Below 256 the runtime's own table has the same bits but no _BLANK for tab and no-break space; iswblank takes tab
//   by name. WEOF is in no class. Each isw function answers the class bits it found, as the runtime's do.
// - The names wctype knows are the twelve classes'; wctrans knows "tolower" (2) and "toupper" (1); any other is 0.
// - towlower and towupper change A-Z and a-z alone, as the "C" locale's do.
#include <string.h>
#include <wctype.h>
#include "grt.h"

namespace {

enum { Upper = 1, Lower = 2, Digit = 4, Space = 8, Punct = 0x10, Control = 0x20, Blank = 0x40, Hex = 0x80,
       Alpha = 0x100 | Upper | Lower };

unsigned short low[256];
bool lowKnown;

unsigned classes(wint_t c)
{
    if (c == WEOF) return 0;
    if (c < 256) {
        if (!lowKnown) {
            for (unsigned i = 0; i < 256; i++) low[i] = (unsigned short)(__grt_char_type((unsigned short)i) & 0x1FF);
            low[0x09] &= (unsigned short)~Blank;
            low[0xA0] &= (unsigned short)~Blank;
            lowKnown = true;
        }
        return low[c];
    }
    return __grt_char_type((unsigned short)c) & 0x1FF;
}

}  // namespace

extern "C" int iswctype(wint_t c, wctype_t mask)
{
    return (int)(classes(c) & mask);
}

extern "C" int iswalnum(wint_t c) { return iswctype(c, Alpha | Digit); }
extern "C" int iswalpha(wint_t c) { return iswctype(c, Alpha); }
extern "C" int iswblank(wint_t c) { return c == L'\t' ? Blank : iswctype(c, Blank); }
extern "C" int iswcntrl(wint_t c) { return iswctype(c, Control); }
extern "C" int iswdigit(wint_t c) { return iswctype(c, Digit); }
extern "C" int iswgraph(wint_t c) { return iswctype(c, Punct | Alpha | Digit); }
extern "C" int iswlower(wint_t c) { return iswctype(c, Lower); }
extern "C" int iswprint(wint_t c) { return iswctype(c, Blank | Punct | Alpha | Digit); }
extern "C" int iswpunct(wint_t c) { return iswctype(c, Punct); }
extern "C" int iswspace(wint_t c) { return iswctype(c, Space); }
extern "C" int iswupper(wint_t c) { return iswctype(c, Upper); }
extern "C" int iswxdigit(wint_t c) { return iswctype(c, Hex); }

extern "C" wctype_t wctype(const char *name)
{
    static const struct { const char *name; unsigned short mask; } names[] = {
        { "alnum", Alpha | Digit }, { "alpha", Alpha }, { "blank", Blank }, { "cntrl", Control }, { "digit", Digit },
        { "graph", Punct | Alpha | Digit }, { "lower", Lower }, { "print", Blank | Punct | Alpha | Digit },
        { "punct", Punct }, { "space", Space }, { "upper", Upper }, { "xdigit", Hex }
    };
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strcmp(name, names[i].name) == 0) return names[i].mask;
    return 0;
}

extern "C" wint_t towlower(wint_t c)
{
    return c >= L'A' && c <= L'Z' ? (wint_t)(c + 32) : c;
}

extern "C" wint_t towupper(wint_t c)
{
    return c >= L'a' && c <= L'z' ? (wint_t)(c - 32) : c;
}

extern "C" wctrans_t wctrans(const char *name)
{
    if (strcmp(name, "tolower") == 0) return 2;
    if (strcmp(name, "toupper") == 0) return 1;
    return 0;
}

extern "C" wint_t towctrans(wint_t c, wctrans_t which)
{
    return which == 2 ? towlower(c) : which == 1 ? towupper(c) : c;
}
