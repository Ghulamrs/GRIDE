/* sort.c - insertion sort: small, stable, and plenty for a sample. */
#include "sort.h"

void sort_ints(int *v, int n)
{
    int i, j, x;
    for (i = 1; i < n; i++) {
        x = v[i];
        for (j = i - 1; j >= 0 && v[j] > x; j--) v[j + 1] = v[j];
        v[j + 1] = x;
    }
}
