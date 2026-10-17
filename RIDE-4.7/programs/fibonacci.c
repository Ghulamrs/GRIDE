/* fibonacci.c - the first twenty Fibonacci numbers, and their ratio. */
#include <stdio.h>

int main(void)
{
    long a = 0, b = 1, t;
    int i;
    for (i = 0; i < 20; i++) {
        printf("%ld ", a);
        t = a + b; a = b; b = t;
    }
    printf("\nratio %.6f\n", (double)b / a);
    return 0;
}
