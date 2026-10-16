// Spec: ISO C 7.20.5.1 - bsearch.

#include <stdlib.h>
#include "Sorting.h"

extern "C" void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*compare)(const void *, const void *))
{
    return rts6x::Sorting::search(key, base, n, size, compare);
}
