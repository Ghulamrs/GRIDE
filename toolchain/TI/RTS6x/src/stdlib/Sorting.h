// Spec: ISO C 7.20.5 - qsort sorts n objects of a size by the comparison, ascending; bsearch finds
// one in an array so sorted, or null. qsort is an introspective quicksort: in place, no allocation,
// heapsort for a range that partitions badly, insertion sort for short ones.
#ifndef RTS6X_SORTING_H
#define RTS6X_SORTING_H

#include <stddef.h>

namespace rts6x {

class Sorting {
public:
    typedef int (*Compare)(const void *, const void *);
    static void sort(void *base, size_t n, size_t size, Compare compare);
    static void *search(const void *key, const void *base, size_t n, size_t size, Compare compare);

private:
    enum { Short = 10 };
    char *base_;
    size_t size_;
    Compare compare_;
    bool words_;
    int shift_;          // log2 of the size when it is a power of two, else -1

    Sorting(char *base, size_t size, Compare compare);
    // Words when the size and the base allow, else bytes.
    void swap(char *a, char *b) const;
    // n elements from lo; a range is its first element and its count.
    void quick(char *lo, size_t n, unsigned depth) const;
    char *partition(char *lo, size_t n) const;
    void insertion(char *lo, size_t n) const;
    void heap(char *lo, size_t n) const;
    // Sinks element i of the heap of the n elements from lo into place.
    void sink(char *lo, size_t i, size_t n) const;
};

}  // namespace rts6x

#endif
