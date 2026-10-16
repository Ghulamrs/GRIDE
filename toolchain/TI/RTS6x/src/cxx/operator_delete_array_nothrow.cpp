// Spec: ISO C++11 18.6.1.2/12 - operator delete[](void *, nothrow_t).

#include <stdlib.h>
#include "Allocation.h"

void operator delete[](void *p, const std::nothrow_t &) throw()
{
    free(p);
}
