/* time32.c - the 32-bit time functions c90's <time.h> declares (c90), in grtcompat.
 *
 * Spec: Microsoft C runtime - time_t counts seconds from 1970-01-01 00:00:00 UTC. c90's <time.h> keeps the
 * 32-bit time_t (long) and the plain names; cpp11's the 64-bit one behind _time64 and the like
 * (c\time64.cpp), and these are those with the value narrowed. The Microsoft runtime has no plain time,
 * localtime, gmtime, mktime, ctime or difftime of its own (they are inline in its headers); they are here so
 * that a program c90 compiled links. */
#include <errno.h>
#include <time.h>

long long _time64(long long *t);
struct tm *_localtime64(const long long *t);
struct tm *_gmtime64(const long long *t);
long long _mktime64(struct tm *fields);
char *_ctime64(const long long *t);

/* a 64-bit time a 32-bit time_t cannot hold: -1, errno EOVERFLOW (132) as the runtime's _time32 says */
static long narrowed(long long t)
{
    if (t > 2147483647LL || t < -2147483647LL - 1) { errno = 132; return -1L; }
    return (long)t;
}

time_t time(time_t *t)
{
    long v = narrowed(_time64(0));
    if (t) *t = v;
    return v;
}

struct tm *localtime(const time_t *t)
{
    long long wide = *t;
    return _localtime64(&wide);
}

struct tm *gmtime(const time_t *t)
{
    long long wide = *t;
    return _gmtime64(&wide);
}

time_t mktime(struct tm *fields)
{
    long long t = _mktime64(fields);
    return t == -1 ? -1L : narrowed(t);
}

char *ctime(const time_t *t)
{
    long long wide = *t;
    return _ctime64(&wide);
}

double difftime(time_t end, time_t start)
{
    return (double)end - (double)start;
}
