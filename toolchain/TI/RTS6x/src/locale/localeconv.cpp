// Spec: ISO C 7.11.2.1 - localeconv: the "C" locale's conventions, "." the decimal point, every other
// string empty and every char CHAR_MAX (127 here, char being signed), meaning "not available".

#include <locale.h>

namespace {

char point[] = ".";
char none[] = "";

}  // namespace

extern "C" struct lconv *localeconv(void)
{
    static struct lconv conventions = { point, none, none, none, none, none, none, none, none, none,
                                        127, 127, 127, 127, 127, 127, 127, 127 };
    return &conventions;
}
