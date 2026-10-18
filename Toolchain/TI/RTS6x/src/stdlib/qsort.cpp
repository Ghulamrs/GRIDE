// Spec: ISO C 7.20.5.2 - qsort.

#include <stdlib.h>
#include "Sorting.h"

extern "C" void qsort(void *base, size_t n, size_t size, int (*compare)(const void *, const void *))
{
    rts6x::Sorting::sort(base, n, size, compare);
}
