/* stats.h - summary figures of a set of numbers. */
#ifndef STATS_H
#define STATS_H
double stats_mean(const int *v, int n);
double stats_median(int *v, int n);     /* sorts v */
int    stats_range(const int *v, int n);
#endif
