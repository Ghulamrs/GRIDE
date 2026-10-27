/* time.c - Windows' clock and time zone (c90).
 *
 * Spec: Microsoft C runtime - time_t counts seconds from 1970-01-01 00:00:00 UTC; clock() the wall
 * time since the program started, in CLOCKS_PER_SEC (1000) units; local time by the time zone Windows
 * is set to, its daylight saving rule applied to the year in question. The time functions themselves are
 * the C library's (c\time64.cpp, 64-bit) and grtcompat's (compat\time32.c, the 32-bit names). */
#include <errno.h>
#include <time.h>
#include "grt.h"
#include "win32.h"

#define EPOCH_TICKS 116444736000000000LL   /* 1970-01-01 in 100 ns ticks from 1601-01-01 */

static long long started, frequency;

void __grt_clock_start(void)
{
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
}

long long __grt_clock_ms(void)
{
    long long now;
    if (frequency <= 0) return 0;
    QueryPerformanceCounter(&now);
    now -= started;
    return now / frequency * 1000 + now % frequency * 1000 / frequency;
}

long long __grt_time_now(void)
{
    GRT_FILETIME f;
    long long ticks;
    GetSystemTimeAsFileTime(&f);
    ticks = (long long)(((unsigned long long)f.high << 32) | f.low);
    return (ticks - EPOCH_TICKS) / 10000000;
}

static void to_filetime(long long t, GRT_FILETIME *f)
{
    unsigned long long ticks = (unsigned long long)(t * 10000000 + EPOCH_TICKS);
    f->low = (DWORD)ticks;
    f->high = (DWORD)(ticks >> 32);
}

static long long from_filetime(const GRT_FILETIME *f)
{
    long long ticks = (long long)(((unsigned long long)f->high << 32) | f->low);
    return (ticks - EPOCH_TICKS) / 10000000;
}

static int day_of_year(int year, int month, int day)
{
    static const int before[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return before[month] + day - 1 + (leap && month > 1);
}

int __grt_local_fields(long long t, int fields[9])
{
    GRT_FILETIME f, back;
    GRT_SYSTEMTIME universal, local;
    GRT_TIME_ZONE_INFORMATION zone;
    long long shift;
    DWORD id;
    to_filetime(t, &f);
    if (!FileTimeToSystemTime(&f, &universal) || !SystemTimeToTzSpecificLocalTime(0, &universal, &local)) return 0;
    fields[0] = local.wSecond;
    fields[1] = local.wMinute;
    fields[2] = local.wHour;
    fields[3] = local.wDay;
    fields[4] = local.wMonth - 1;
    fields[5] = local.wYear - 1900;
    fields[6] = local.wDayOfWeek;
    fields[7] = day_of_year(local.wYear, local.wMonth - 1, local.wDay);
    fields[8] = 0;
    /* daylight saving time when the shift from UTC is the zone's daylight one */
    id = GetTimeZoneInformation(&zone);
    if (id != GRT_TIME_ZONE_ID_INVALID && zone.DaylightDate.wMonth != 0 && zone.DaylightBias != zone.StandardBias &&
        SystemTimeToFileTime(&local, &back)) {
        shift = (t - from_filetime(&back)) / 60;          /* minutes west of UTC */
        fields[8] = shift == zone.Bias + zone.DaylightBias;
    }
    return 1;
}

long long __grt_from_local(const int fields[9], int isdst)
{
    GRT_SYSTEMTIME local, universal;
    GRT_FILETIME f;
    long long t;
    (void)isdst;
    if (fields[5] + 1900 < 1601 || fields[5] + 1900 > 30827) return -1;
    local.wYear = (unsigned short)(fields[5] + 1900);
    local.wMonth = (unsigned short)(fields[4] + 1);
    local.wDayOfWeek = 0;
    local.wDay = (unsigned short)fields[3];
    local.wHour = (unsigned short)fields[2];
    local.wMinute = (unsigned short)fields[1];
    local.wSecond = (unsigned short)fields[0];
    local.wMilliseconds = 0;
    if (!TzSpecificLocalTimeToSystemTime(0, &local, &universal) || !SystemTimeToFileTime(&universal, &f)) return -1;
    t = from_filetime(&f);
    return t;
}

/* Spec: Microsoft C runtime, _stat and _findfirst - a file's time goes to local time by Windows' rule and back
   by the runtime's own (its __loctotime64_t), which refuses years outside 1970..3000 with EINVAL. */
long long __grt_file_time(unsigned long low, unsigned long high)
{
    GRT_FILETIME f;
    GRT_SYSTEMTIME universal, local;
    int fields[9];
    f.low = low;
    f.high = high;
    if (!FileTimeToSystemTime(&f, &universal) || !SystemTimeToTzSpecificLocalTime(0, &universal, &local)) return -1;
    if (local.wYear < 1970 || local.wYear > 3000) { errno = EINVAL; return -1; }
    fields[0] = local.wSecond;
    fields[1] = local.wMinute;
    fields[2] = local.wHour;
    fields[3] = local.wDay;
    fields[4] = local.wMonth - 1;
    fields[5] = local.wYear - 1900;
    fields[6] = fields[7] = 0;
    fields[8] = -1;
    return __grt_from_local(fields, -1);
}

static char zone_names[2][64];

const char *__grt_zone_name(int isdst)
{
    GRT_TIME_ZONE_INFORMATION zone;
    int k = isdst > 0;
    if (GetTimeZoneInformation(&zone) == GRT_TIME_ZONE_ID_INVALID) return "";
    if (!WideCharToMultiByte(GRT_CP_ACP, 0, k ? zone.DaylightName : zone.StandardName, -1, zone_names[k], 64, 0, 0))
        zone_names[k][0] = 0;
    return zone_names[k];
}

long __grt_zone_minutes(int isdst)
{
    GRT_TIME_ZONE_INFORMATION zone;
    if (GetTimeZoneInformation(&zone) == GRT_TIME_ZONE_ID_INVALID) return 0;
    return -(zone.Bias + (isdst > 0 ? zone.DaylightBias : zone.StandardBias));
}

/* the 64-bit functions of win\time64.cpp, as c90 sees them */
