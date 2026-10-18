// Spec: ISO C++11 18.6.1.2/9-11 - operator delete[](void *) is operator delete(p).

#include <stdlib.h>
#include "Allocation.h"

void operator delete[](void *p) throw()
{
    free(p);
}
