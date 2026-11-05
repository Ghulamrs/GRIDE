/* time32.c - the 32-bit time functions c90's <time.h> declared before stage 3 (c90), in grtcompat.
 *
 * Spec: Microsoft C runtime - time_t counts seconds from 1970-01-01 00:00:00 UTC. The headers now give both
 * compilers the 64-bit time_t behind _time64 and the like (c\time64.cpp; docs\Runtime_Design.md 5.2), as macros
 * under c90; these are the plain names with the 32-bit time_t (long) and the value narrowed, kept so that an object
 * c90 compiled before still links. The Microsoft runtime has no plain time, localtime, gmtime, mktime, ctime or
 * difftime of its own (they are inline in its headers). */
#include <errno.h>
#include <time.h>

#undef time
#undef localtime
#undef gmtime
#undef mktime
#undef ctime
#undef difftime

/* a 64-bit time a 32-bit time_t cannot hold: -1, errno EOVERFLOW (132) as the runtime's _time32 says */
static long grt_narrowed(long long t)
{
    if (t > 2147483647LL || t < -2147483647LL - 1) { errno = 132; return -1L; }
    return (long)t;
}

long time(long *t)
{
    long v = grt_narrowed(_time64(0));
    if (t) *t = v;
    return v;
}

struct tm *localtime(const long *t)
{
    long long wide = *t;
    return _localtime64(&wide);
}

struct tm *gmtime(const long *t)
{
    long long wide = *t;
    return _gmtime64(&wide);
}

long mktime(struct tm *fields)
{
    long long t = _mktime64(fields);
    return t == -1 ? -1L : grt_narrowed(t);
}

char *ctime(const long *t)
{
    long long wide = *t;
    return _ctime64(&wide);
}

double difftime(long end, long start)
{
    return (double)end - (double)start;
}
