// Spec: ISO C 7.23.3.2 - ctime(t) is asctime(localtime(t)).

#include <time.h>

extern "C" char *ctime(const time_t *t)
{
    return asctime(localtime(t));
}
