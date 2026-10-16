// Spec: ISO C 7.20.1.1 - atof(s) is strtod(s, (char **)NULL), errno aside.

#include <stdlib.h>

extern "C" double atof(const char *s)
{
    return strtod(s, 0);
}
