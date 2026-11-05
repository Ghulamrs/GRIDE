/* divide.c - div and ldiv from C, whose compiler must call a function named as an instruction, in a file
   that also divides: an unsigned division of 32 and of 64 bits is written as bytes there (Compiler-Ci's Masm.cpp). */
#include <stdio.h>
#include <stdlib.h>

static unsigned narrow(unsigned a, unsigned b) { return a / b; }
static size_t wide(size_t a, size_t b) { return a / b; }

int main(void)
{
    div_t d = div(17, 5);
    div_t n = div(-17, 5);
    ldiv_t l = ldiv(-7L, 2L);
    printf("div %d %d, %d %d\n", d.quot, d.rem, n.quot, n.rem);
    printf("ldiv %ld %ld\n", l.quot, l.rem);
    printf("unsigned %u\n", narrow(4000000000u, 7u));
    printf("wide %u\n", (unsigned)wide((size_t)4000000000u, (size_t)9u));
    return 0;
}