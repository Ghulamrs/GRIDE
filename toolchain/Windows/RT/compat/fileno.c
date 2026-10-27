/* fileno.c - POSIX's fileno, the name Microsoft keeps in oldnames.lib for _fileno (c90), in grtcompat. */
#include <stdio.h>

int _fileno(FILE *stream);

int fileno(FILE *stream)
{
    return _fileno(stream);
}
