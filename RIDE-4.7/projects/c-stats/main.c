/* main.c - the figures for one week of temperatures. */
#include <stdio.h>
#include "stats.h"

int main(void)
{
    int week[7] = { 21, 25, 19, 30, 27, 23, 22 };
    int i;

    printf("mean   %.2f\n", stats_mean(week, 7));
    printf("range  %d\n", stats_range(week, 7));
    printf("median %.1f\n", stats_median(week, 7));
    printf("sorted");
    for (i = 0; i < 7; i++) printf(" %d", week[i]);
    printf("\n");
    return 0;
}
