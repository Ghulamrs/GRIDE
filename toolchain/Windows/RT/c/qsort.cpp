// Spec: ISO C 7.20.5.2 - qsort; the order of elements that compare equal is the Microsoft runtime's, so a program
// sorting records by one key prints the same lines with either runtime. Its algorithm, followed step by step: a
// part of 8 or fewer elements is sorted by selection, the greatest (the first of equal ones) swapped to its end; a
// larger part is split around the median of its first, middle and last elements, the elements equal to the pivot
// passed over at the split's end, and the smaller side sorted next while the larger waits on a stack of 62 parts.
// Elements of 4 and 8 bytes are sorted by c\qsortword.asm, the same steps with every pointer in a register; others
// here, swapped 8 bytes at a time, then 4, then bytes. A null base with elements, a width of 0 or a null comparison
// fails fast, as the runtime's check of its parameters does.
#include <stddef.h>
#include "grt.h"

typedef int (*Compare)(const void *, const void *);

extern "C" void __grt_qsort4(void *base, size_t count, Compare compare);
extern "C" void __grt_qsort8(void *base, size_t count, Compare compare);

namespace {

using grt::bytesBetween;

void swap(char *a, char *b, size_t width)
{
    if (a == b) return;
    while (width >= 8) {
        unsigned long long t = *(unsigned long long *)a;
        *(unsigned long long *)a = *(unsigned long long *)b;
        *(unsigned long long *)b = t;
        a += 8;
        b += 8;
        width -= 8;
    }
    if (width >= 4) {
        unsigned t = *(unsigned *)a;
        *(unsigned *)a = *(unsigned *)b;
        *(unsigned *)b = t;
        a += 4;
        b += 4;
        width -= 4;
    }
    while (width--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

void shortSort(char *lo, char *hi, size_t width, Compare compare)
{
    while (hi > lo) {
        char *max = lo;
        for (char *p = lo + width; p <= hi; p += width)
            if (compare(p, max) > 0) max = p;
        swap(max, hi, width);
        hi -= width;
    }
}

}

extern "C" void qsort(void *base, size_t count, size_t width, Compare compare)
{
    if ((!base && count != 0) || width == 0 || !compare) __grt_fail_fast();
    if (count < 2) return;
    if (width == 4) { __grt_qsort4(base, count, compare); return; }
    if (width == 8) { __grt_qsort8(base, count, compare); return; }
    char *loStack[62], *hiStack[62];
    int depth = 0;
    char *lo = (char *)base;
    char *hi = lo + width * (count - 1);
    for (;;) {
        size_t size = bytesBetween(lo, hi) / width + 1;
        if (size <= 8) {
            shortSort(lo, hi, width, compare);
        } else {
            char *mid = lo + (size >> 1) * width;
            if (compare(lo, mid) > 0) swap(lo, mid, width);
            if (compare(lo, hi) > 0) swap(lo, hi, width);
            if (compare(mid, hi) > 0) swap(mid, hi, width);
            char *up = lo, *down = hi;
            for (;;) {
                if (mid > up) {
                    do up += width; while (up < mid && compare(up, mid) <= 0);
                }
                if (mid <= up) {
                    do up += width; while (up <= hi && compare(up, mid) <= 0);
                }
                do down -= width; while (down > mid && compare(down, mid) > 0);
                if (down < up) break;
                swap(up, down, width);
                if (mid == down) mid = up;
            }
            down += width;
            if (mid < down) {
                do down -= width; while (down > mid && compare(down, mid) == 0);
            }
            if (mid >= down) {
                do down -= width; while (down > lo && compare(down, mid) == 0);
            }
            if ((long long)bytesBetween(lo, down) >= (long long)bytesBetween(up, hi)) {
                if (lo < down) { loStack[depth] = lo; hiStack[depth] = down; ++depth; }
                if (up < hi) { lo = up; continue; }
            } else {
                if (up < hi) { loStack[depth] = up; hiStack[depth] = hi; ++depth; }
                if (lo < down) { hi = down; continue; }
            }
        }
        if (--depth < 0) return;
        lo = loStack[depth];
        hi = hiStack[depth];
    }
}
