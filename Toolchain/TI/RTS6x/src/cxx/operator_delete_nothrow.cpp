// Spec: ISO C++11 18.6.1.1/13 - operator delete(void *, nothrow_t), called when a constructor throws.

#include <stdlib.h>
#include "Allocation.h"

void operator delete(void *p, const std::nothrow_t &) throw()
{
    free(p);
}
