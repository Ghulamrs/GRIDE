// Spec: Microsoft C runtime, _msize - the size of a block malloc, calloc or realloc gave: here its usable size, at
// least what was asked for (docs\Runtime_Design.md 9.4, the deviation recorded). A null block answers -1 and EINVAL,
// where the UCRT ends the program.

#include <errno.h>
#include <malloc.h>
#include "Heap.h"
#include "../misc/ErrorNumber.h"

extern "C" size_t _msize(void *block)
{
    if (block == 0) {
        errno = EINVAL;
        return static_cast<size_t>(-1);
    }
    return rts6x::Heap::usable(block);
}