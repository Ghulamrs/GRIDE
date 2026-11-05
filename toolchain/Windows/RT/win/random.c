/* random.c - rand_s (c90), the Microsoft runtime's: four bytes from advapi32's RtlGenRandom (exported as
 * SystemFunction036), the generator that runtime reads. advapi32 is loaded when rand_s is first called, so a
 * program still imports KERNEL32.dll alone.
 *
 * Spec: the Microsoft runtime's rand_s - a null pointer fails fast, as its check of its parameters does; with no
 * random bytes the value is 0 and the answer, and errno, ENOMEM. */
#include <errno.h>
#include "grt.h"
#include "win32.h"

typedef unsigned char (*Generator)(void *buffer, unsigned long length);

static Generator generator;
static int looked;

int rand_s(unsigned int *value)
{
    static const WCHAR advapi32[] = { 'a', 'd', 'v', 'a', 'p', 'i', '3', '2', '.', 'd', 'l', 'l', 0 };
    if (!value) __grt_fail_fast();
    *value = 0;
    if (!looked) {
        HANDLE module = LoadLibraryW(advapi32);
        generator = module ? (Generator)GetProcAddress(module, "SystemFunction036") : 0;
        looked = 1;
    }
    if (!generator || !generator(value, sizeof *value)) {
        errno = ENOMEM;
        return ENOMEM;
    }
    return 0;
}
