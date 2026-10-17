/* stats.c - mean, median and range. */
#include "stats.h"
#include "sort.h"

double stats_mean(const int *v, int n)
{
    long sum = 0;
    int i;
    for (i = 0; i < n; i++) sum += v[i];
    return (double)sum / n;
}

double stats_median(int *v, int n)
{
    sort_ints(v, n);
    if (n % 2 == 1) return v[n / 2];
    return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

int stats_range(const int *v, int n)
{
    int lo = v[0], hi = v[0], i;
    for (i = 1; i < n; i++) {
        if (v[i] < lo) lo = v[i];
        if (v[i] > hi) hi = v[i];
    }
    return hi - lo;
}
