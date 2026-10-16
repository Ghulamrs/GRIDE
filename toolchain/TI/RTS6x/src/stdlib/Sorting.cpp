// Spec: ISO C 7.20.5.1-2. Quicksort over the median of three, partitioned from both ends (Hoare), the
// shorter side first and the longer as a loop; past 2 log2 n levels a range is heapsorted, so the
// worst case stays n log n; ranges of Short elements or fewer are finished by insertion.

#include "Sorting.h"

namespace rts6x {

Sorting::Sorting(char *base, size_t size, Compare compare)
    : base_(base), size_(size), compare_(compare), words_((((size_t)base | size) & 3) == 0), shift_(-1)
{
    for (int k = 0; k < 31; k++)
        if (size == (size_t)1 << k) shift_ = k;
}

void Sorting::swap(char *a, char *b) const
{
    if (words_) {
        unsigned *x = (unsigned *)a, *y = (unsigned *)b;
        size_t n = size_ >> 2;
        for (size_t k = 0; k < n; k++) { unsigned t = x[k]; x[k] = y[k]; y[k] = t; }
        return;
    }
    for (size_t k = 0; k < size_; k++) { char t = a[k]; a[k] = b[k]; b[k] = t; }
}

void Sorting::insertion(char *lo, size_t n) const
{
    size_t s = size_;
    Compare compare = compare_;
    char *end = lo + n * s;
    for (char *i = lo + s; i < end; i += s)
        for (char *j = i; j > lo && compare(j, j - s) < 0; j -= s) swap(j, j - s);
}

void Sorting::sink(char *lo, size_t i, size_t n) const
{
    Compare compare = compare_;
    for (;;) {
        size_t largest = i, left = 2 * i + 1, right = left + 1;
        if (left < n && compare(lo + largest * size_, lo + left * size_) < 0) largest = left;
        if (right < n && compare(lo + largest * size_, lo + right * size_) < 0) largest = right;
        if (largest == i) return;
        swap(lo + i * size_, lo + largest * size_);
        i = largest;
    }
}

void Sorting::heap(char *lo, size_t n) const
{
    for (size_t i = n / 2; i-- > 0;) sink(lo, i, n);
    for (size_t end = n - 1; end > 0; end--) {
        swap(lo, lo + end * size_);
        sink(lo, 0, end);
    }
}

// The first, middle and last put in order, the median moved next to last as the pivot; i and j meet
// over the rest, each stopping at an element equal to the pivot. The pivot's final place is returned.
char *Sorting::partition(char *lo, size_t n) const
{
    size_t s = size_;
    Compare compare = compare_;
    char *hi = lo + (n - 1) * s, *mid = lo + (n / 2) * s;
    if (compare(mid, lo) < 0) swap(mid, lo);
    if (compare(hi, mid) < 0) {
        swap(hi, mid);
        if (compare(mid, lo) < 0) swap(mid, lo);
    }
    char *pivot = hi - s, *i = lo, *j = pivot;
    swap(mid, pivot);
    for (;;) {
        do i += s; while (compare(i, pivot) < 0);
        do j -= s; while (compare(pivot, j) < 0);
        if (i >= j) break;
        swap(i, j);
    }
    swap(i, pivot);
    return i;
}

void Sorting::quick(char *lo, size_t n, unsigned depth) const
{
    while (n > Short) {
        if (depth-- == 0) { heap(lo, n); return; }
        char *p = partition(lo, n);
        size_t bytes = (size_t)(p - lo), left = shift_ >= 0 ? bytes >> shift_ : bytes / size_;
        size_t right = n - left - 1;
        if (left < right) {
            quick(lo, left, depth);
            lo = p + size_;
            n = right;
        } else {
            quick(p + size_, right, depth);
            n = left;
        }
    }
    if (n > 1) insertion(lo, n);
}

void Sorting::sort(void *base, size_t n, size_t size, Compare compare)
{
    if (n < 2 || size == 0) return;
    unsigned depth = 0;
    for (size_t m = n; m > 1; m >>= 1) depth += 2;
    Sorting(static_cast<char *>(base), size, compare).quick(static_cast<char *>(base), n, depth);
}

void *Sorting::search(const void *key, const void *base, size_t n, size_t size, Compare compare)
{
    const char *b = static_cast<const char *>(base);
    while (n != 0) {
        size_t half = n >> 1;
        const char *middle = b + half * size;
        int c = compare(key, middle);
        if (c == 0) return (void *)middle;
        if (c > 0) { b = middle + size; n -= half + 1; }
        else n = half;
    }
    return 0;
}

}  // namespace rts6x
