// Spec: ISO C++11 18.6.1.1/3-4 - the default operator new: a loop that calls malloc and, when it
// fails, the current new handler if there is one; with none, std::bad_alloc thrown.

#include <stdlib.h>
#include "Allocation.h"

extern "C" void __rts6x_bad_alloc(void);

namespace rts6x {

std::new_handler Allocation::handler_;

std::new_handler Allocation::exchange(std::new_handler next)
{
    std::new_handler old = handler_;
    handler_ = next;
    return old;
}

void *Allocation::tryAllocate(size_t size)
{
    for (;;) {
        void *p = malloc(size ? size : 1);
        if (p) return p;
        std::new_handler h = handler_;
        if (!h) return 0;
        h();
    }
}

void *Allocation::allocate(size_t size)
{
    void *p = tryAllocate(size);
    if (!p) __rts6x_bad_alloc();
    return p;
}

}  // namespace rts6x
