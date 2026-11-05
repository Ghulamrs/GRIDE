// Spec: ISO C 7.11.1.1 - setlocale. "C" is the only locale here: "", "C" and "POSIX" select it, a null name asks which
// is selected, and an unknown name is refused with a null pointer. Beside it, the switch of docs\Runtime_Design.md 6,
// spelled as the Microsoft runtime spells a UTF-8 code page with no language: ".UTF-8", ".UTF8" or ".65001", in any
// case, for LC_ALL or LC_CTYPE, makes the program's narrow strings UTF-8, until "C" or the like asks them back.
// - Only LC_CTYPE changes: the other categories stay "C", where the Microsoft runtime's LC_ALL takes the user's
//   language too, and they refuse the name. A language ("de_DE.UTF-8") is refused, there being no data for one.
// - The answer is ".UTF-8", which given back selects the same again.
#include <locale.h>
#include "grt.h"

namespace {

const char Utf8Name[] = ".UTF-8";

bool sameLetters(const char *name, const char *lower)
{
    for (; *lower; name++, lower++) {
        char c = *name;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != *lower) return false;
    }
    return !*name;
}

bool sameText(const char *name, const char *text)
{
    while (*text && *name == *text) { name++; text++; }
    return !*name && !*text;
}

bool isUtf8(const char *name)
{
    return sameLetters(name, ".utf-8") || sameLetters(name, ".utf8") || sameLetters(name, ".65001");
}

}  // namespace

extern "C" char *setlocale(int category, const char *name)
{
    const bool encoding = category == LC_ALL || category == LC_CTYPE;
    if (!name) return (char *)(encoding && __grt_utf8 ? Utf8Name : "C");
    if (isUtf8(name)) {
        if (!encoding) return 0;
        __grt_set_utf8(1);
        return (char *)Utf8Name;
    }
    if (name[0] && !sameText(name, "C") && !sameText(name, "POSIX")) return 0;
    if (encoding) __grt_set_utf8(0);
    return (char *)"C";
}
