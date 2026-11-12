// Spec: Microsoft C runtime, _aligned_free - a block _aligned_malloc gave back to malloc; a null one is nothing.

#include <malloc.h>

extern "C" void _aligned_free(void *block)
{
    if (block != 0) free(static_cast<void **>(block)[-1]);
}