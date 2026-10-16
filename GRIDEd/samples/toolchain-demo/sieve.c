/* sieve.c - the primes below 100 by Eratosthenes' sieve, their count and sum, and a gcd.
 *
 * Example 1 compiles it with c90 for Windows (masm, link); example 3 converts it with c2s to
 * Shalimar and runs that on the C6747 (vm6747, and asm6x / lnk6x / sim6747). Both print the same
 * lines: the C and the Shalimar program agree.
 */

#include <stdio.h>

int gcd(int a, int b)
{
    int r;

    while (b != 0) {
        r = a % b;
        a = b;
        b = r;
    }
    return a;
}

int main(void)
{
    int sieve[100];
    int i;
    int j;
    int count = 0;
    int sum = 0;

    for (i = 0; i < 100; i++) {
        sieve[i] = 1;
    }
    sieve[0] = 0;
    sieve[1] = 0;
    for (i = 2; i * i < 100; i++) {
        if (sieve[i] == 1) {
            for (j = i * i; j < 100; j = j + i) {
                sieve[j] = 0;
            }
        }
    }
    for (i = 0; i < 100; i++) {
        if (sieve[i] == 1) {
            count = count + 1;
            sum = sum + i;
        }
    }
    /* a value is followed by a space or the line's end: Shalimar's ? writes a space after every item */
    printf("primes below 100: %d and their sum: %d\n", count, sum);
    printf("gcd of 1071 and 462 is %d\n", gcd(1071, 462));
    return 0;
}
