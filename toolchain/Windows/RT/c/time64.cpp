// Spec: ISO C 7.23 with the Microsoft runtime's 64-bit time_t, as cpp11's <time.h> declares it: _time64,
// _gmtime64, _localtime64, _mktime64, _ctime64 and _difftime64 (time, gmtime and the rest are inline
// over them there), and clock. The calendar is RTS6x's (time\Calendar.cpp); local time is Windows'
// (time.c). The runtime's range: 1970-01-01 to 3000-12-31 23:59:59 - outside it a null pointer or -1,
// errno EINVAL.

#include <errno.h>
#include <time.h>
#include "time/Calendar.h"
#include "grt.h"

namespace {

const long long latest = 32535215999LL;   // 3000-12-31 23:59:59 UTC

struct tm result;

void fill(const int fields[9], struct tm *out)
{
    out->tm_sec = fields[0]; out->tm_min = fields[1]; out->tm_hour = fields[2];
    out->tm_mday = fields[3]; out->tm_mon = fields[4]; out->tm_year = fields[5];
    out->tm_wday = fields[6]; out->tm_yday = fields[7]; out->tm_isdst = fields[8];
}

}  // namespace

extern "C" time_t _time64(time_t *t)
{
    time_t now = (time_t)__grt_time_now();
    if (t) *t = now;
    return now;
}

extern "C" double _difftime64(time_t end, time_t start)
{
    return (double)end - (double)start;
}

extern "C" struct tm *_gmtime64(const time_t *t)
{
    if (!t || *t < 0 || *t > latest) { errno = EINVAL; return 0; }
    rts6x::Calendar::breakDown(*t, &result);
    return &result;
}

extern "C" struct tm *_localtime64(const time_t *t)
{
    int fields[9];
    if (!t || *t < 0 || *t > latest || !__grt_local_fields(*t, fields)) { errno = EINVAL; return 0; }
    fill(fields, &result);
    return &result;
}

extern "C" time_t _mktime64(struct tm *tm)
{
    // the fields normalised as a calendar date (seconds into minutes, days into months), then read as
    // local time, and tm set to what that time is - its weekday, its day of the year, its daylight saving
    struct tm normal = *tm;
    rts6x::Calendar::compose(&normal);
    int fields[9] = { normal.tm_sec, normal.tm_min, normal.tm_hour, normal.tm_mday, normal.tm_mon, normal.tm_year,
                      normal.tm_wday, normal.tm_yday, tm->tm_isdst };
    long long t = __grt_from_local(fields, tm->tm_isdst);
    if (t < 0 || t > latest) { errno = EINVAL; return -1; }
    int local[9];
    if (__grt_local_fields(t, local)) fill(local, tm);
    return (time_t)t;
}

extern "C" char *_ctime64(const time_t *t)
{
    struct tm *local = _localtime64(t);
    return local ? asctime(local) : 0;
}

extern "C" clock_t clock(void)
{
    return (clock_t)__grt_clock_ms();
}
