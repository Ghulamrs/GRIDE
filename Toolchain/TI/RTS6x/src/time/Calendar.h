// Spec: ISO C 7.23.1 (struct tm), 7.23.3.3 gmtime and 7.23.2.3 mktime (fields normalised into their
// ranges); the proleptic Gregorian calendar (a leap year every fourth, but not a hundredth, but a
// four-hundredth) and the epoch of time_t, 1970-01-01 00:00:00 UTC, a Thursday.
#ifndef RTS6X_CALENDAR_H
#define RTS6X_CALENDAR_H

#include <time.h>

namespace rts6x {

class Calendar {
public:
    // t broken down as UTC.
    static void breakDown(time_t t, tm *out);
    // tm's fields normalised, tm_wday and tm_yday set, and the time it names; -1 out of range.
    static time_t compose(tm *fields);

    static bool leap(long year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }
    static int daysInMonth(long year, int month);
    // Days from 1970-01-01 to day `day` (1-based) of month (0-11) of year, negative before it.
    static long daysFromEpoch(long year, int month, int day);

private:
    // Floor division and its remainder, for the negative values before the epoch.
    static long floorDiv(long a, long b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }
    static long floorMod(long a, long b) { return a - floorDiv(a, b) * b; }
    static long leapsBefore(long year) { long y = year - 1; return floorDiv(y, 4) - floorDiv(y, 100) + floorDiv(y, 400); }
    static int dayOfYear(long year, int month, int day);
};

}  // namespace rts6x

#endif
