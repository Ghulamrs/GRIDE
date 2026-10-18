// Spec: ISO C 7.19.7.9 - putchar(c) is putc(c, stdout).

#include <stdio.h>

extern "C" int putchar(int c)
{
    return fputc(c, stdout);
}
