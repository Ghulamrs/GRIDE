// Spec: the Gregorian calendar of ISO C 7.23 - day counts by year and month from the epoch, and back.

#include "Calendar.h"

namespace rts6x {

int Calendar::daysInMonth(long year, int month)
{
    static const unsigned char days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return month == 1 && leap(year) ? 29 : days[month];
}

int Calendar::dayOfYear(long year, int month, int day)
{
    int n = day - 1;
    for (int m = 0; m < month; m++) n += daysInMonth(year, m);
    return n;
}

long Calendar::daysFromEpoch(long year, int month, int day)
{
    return 365 * (year - 1970) + (leapsBefore(year) - leapsBefore(1970)) + dayOfYear(year, month, day);
}

void Calendar::breakDown(time_t t, tm *out)
{
    long days = floorDiv(t, 86400);
    long seconds = floorMod(t, 86400);
    long year = 1970 + floorDiv(days, 365);
    while (daysFromEpoch(year, 0, 1) > days) year--;
    while (daysFromEpoch(year + 1, 0, 1) <= days) year++;
    int yday = (int)(days - daysFromEpoch(year, 0, 1));
    int month = 0, rest = yday;
    while (rest >= daysInMonth(year, month)) rest -= daysInMonth(year, month++);
    out->tm_year = (int)(year - 1900);
    out->tm_mon = month;
    out->tm_mday = rest + 1;
    out->tm_yday = yday;
    out->tm_wday = (int)floorMod(days + 4, 7);
    out->tm_hour = (int)(seconds / 3600);
    out->tm_min = (int)(seconds / 60 % 60);
    out->tm_sec = (int)(seconds % 60);
    out->tm_isdst = 0;
    out->tm_gmtoff = 0;
    out->tm_zone = "UTC";
}

time_t Calendar::compose(tm *fields)
{
    long seconds = fields->tm_sec, minutes = fields->tm_min, hours = fields->tm_hour;
    minutes += floorDiv(seconds, 60);
    seconds = floorMod(seconds, 60);
    hours += floorDiv(minutes, 60);
    minutes = floorMod(minutes, 60);
    long days = fields->tm_mday - 1 + floorDiv(hours, 24);
    hours = floorMod(hours, 24);
    long year = 1900L + fields->tm_year + floorDiv(fields->tm_mon, 12);
    int month = (int)floorMod(fields->tm_mon, 12);
    long total = daysFromEpoch(year, month, 1) + days;
    // 2^31 seconds is 24855 days and a part: the sum is formed in 64 bits and a 32-bit time_t must hold it.
    if (total < -24856 || total > 24855) return (time_t)-1;
    long long t = (long long)total * 86400 + hours * 3600 + minutes * 60 + seconds;
    if (t < -2147483647LL - 1 || t > 2147483647LL) return (time_t)-1;
    breakDown((time_t)t, fields);
    return (time_t)t;
}

}  // namespace rts6x
