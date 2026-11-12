// Spec: Microsoft C runtime, _aligned_malloc - size bytes at a multiple of alignment, a power of two, from malloc:
// the block malloc gives holds the aligned one, with malloc's own pointer in the word before it for _aligned_free.
// An alignment that is not a power of two answers null and EINVAL, where the UCRT ends the program; no storage,
// null and ENOMEM.

#include <errno.h>
#include <malloc.h>
#include <stdint.h>
#include "../misc/ErrorNumber.h"

extern "C" void *_aligned_malloc(size_t size, size_t alignment)
{
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        errno = EINVAL;
        return 0;
    }
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (size > static_cast<size_t>(-1) - alignment - sizeof(void *)) {
        errno = ENOMEM;
        return 0;
    }
    char *raw = static_cast<char *>(malloc(size + alignment - 1 + sizeof(void *)));
    if (raw == 0) return 0;
    uintptr_t at = (reinterpret_cast<uintptr_t>(raw) + sizeof(void *) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    reinterpret_cast<void **>(at)[-1] = raw;
    return reinterpret_cast<void *>(at);
}