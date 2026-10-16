// Spec: ISO C 7.19.7.8 - putc is fputc.

#include <stdio.h>

extern "C" int putc(int c, FILE *stream)
{
    return fputc(c, stream);
}
