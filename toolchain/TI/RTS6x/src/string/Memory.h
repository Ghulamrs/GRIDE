// Spec: ISO C 7.21.2-7.21.6 - the mem* functions: copy (no overlap), move (overlap allowed), fill,
// compare as unsigned char, find a byte. copy, move and fill are assembly (memcpy.s, memmove.s,
// memset.s: doublewords at any alignment); compare goes a word at a time where both are aligned.
#ifndef RTS6X_MEMORY_H
#define RTS6X_MEMORY_H

#include <stddef.h>

extern "C" void *memcpy(void *to, const void *from, size_t n);
extern "C" void *memmove(void *to, const void *from, size_t n);
extern "C" void *memset(void *to, int value, size_t n);

namespace rts6x {

class Memory {
public:
    static void *copy(void *to, const void *from, size_t n) { return memcpy(to, from, n); }
    static void *move(void *to, const void *from, size_t n) { return memmove(to, from, n); }
    static void *fill(void *to, int value, size_t n) { return memset(to, value, n); }
    static int compare(const void *a, const void *b, size_t n);
    static void *find(const void *in, int value, size_t n);

private:
    static bool aligned(const void *a, const void *b) { return (((size_t)a | (size_t)b) & 3) == 0; }
};

}  // namespace rts6x

#endif
