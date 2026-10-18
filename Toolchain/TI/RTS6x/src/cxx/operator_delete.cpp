// Spec: ISO C++11 18.6.1.1/10-12 - operator delete(void *): the storage back to the heap; null does nothing.

#include <stdlib.h>
#include "Allocation.h"

void operator delete(void *p) throw()
{
    free(p);
}
