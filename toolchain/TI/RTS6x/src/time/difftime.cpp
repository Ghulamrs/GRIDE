// Spec: ISO C 7.23.2.2 - difftime: end - start, in seconds, as a double.

#include <time.h>

extern "C" double difftime(time_t end, time_t start)
{
    return (double)end - (double)start;
}
