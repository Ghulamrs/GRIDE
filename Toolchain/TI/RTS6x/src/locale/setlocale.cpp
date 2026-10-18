// Spec: ISO C 7.11.1.1 - setlocale: "C" is the only locale here; "", "C" and "POSIX" select it,
// a null name asks which is selected, anything else is refused with a null pointer.

#include <locale.h>

extern "C" char *setlocale(int category, const char *name)
{
    (void)category;
    if (!name || !name[0] || (name[0] == 'C' && !name[1])) return (char *)"C";
    const char *posix = "POSIX";
    int i = 0;
    while (posix[i] && name[i] == posix[i]) i++;
    return !posix[i] && !name[i] ? (char *)"C" : 0;
}
