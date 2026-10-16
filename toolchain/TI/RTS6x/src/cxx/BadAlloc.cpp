// Spec: ISO C++11 18.6.1.1/3 and 18.6.2.1 - a failed allocation with no new handler throws
// std::bad_alloc; the class is <new>'s, so the object thrown is the one a program's handler names.

#include <new>

extern "C" void __rts6x_bad_alloc(void)
{
    throw std::bad_alloc();
}
