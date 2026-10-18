// Spec: ISO C 7.23.3.5/3-7 - a A b B c C d D e F g G h H I j m M n p r R S t T u U V w W x X y Y z
// Z %, in the "C" locale: English names, %c as "%a %b %e %H:%M:%S %Y", %x as %m/%d/%y, %X as %T.

#include "TimeFormatter.h"
#include "Calendar.h"

namespace rts6x {

namespace {

const char *const weekdays[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
const char *const months[12] = { "January", "February", "March", "April", "May", "June", "July",
                                 "August", "September", "October", "November", "December" };

}  // namespace

void TimeFormatter::put(char c)
{
    if (count_ + 1 < capacity_) dst_[count_] = c;
    else overflow_ = true;
    count_++;
}

void TimeFormatter::number(long v, int width, char pad)
{
    char digits[16];
    int n = 0;
    bool negative = v < 0;
    unsigned long u = negative ? 0ul - (unsigned long)v : (unsigned long)v;
    do { digits[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (negative) put('-');
    for (int i = n; i < width; i++) put(pad);
    while (n) put(digits[--n]);
}

int TimeFormatter::isoWeek(const tm *fields, long &year)
{
    year = 1900L + fields->tm_year;
    int weekday = (fields->tm_wday + 6) % 7;
    int week = (fields->tm_yday - weekday + 10) / 7;
    if (week < 1) {
        year--;
        int days = Calendar::leap(year) ? 366 : 365;
        week = (fields->tm_yday + days - weekday + 10) / 7;
    } else if (week == 53) {
        int days = Calendar::leap(year) ? 366 : 365;
        if (fields->tm_yday - weekday + 3 >= days) { week = 1; year++; }
    }
    return week;
}

size_t TimeFormatter::run(const char *format, const tm *fields)
{
    while (*format) {
        if (*format != '%') { put(*format++); continue; }
        format++;
        if (*format == 'E' || *format == 'O') format++;
        if (!*format) break;
        conversion(*format++, fields);
    }
    if (capacity_ > 0) dst_[count_ < capacity_ ? count_ : capacity_ - 1] = 0;
    return overflow_ || count_ >= capacity_ ? 0 : count_;
}

void TimeFormatter::conversion(char c, const tm *fields)
{
    long year = 1900L + fields->tm_year, isoYear;
    int wday = fields->tm_wday >= 0 && fields->tm_wday < 7 ? fields->tm_wday : 0;
    int mon = fields->tm_mon >= 0 && fields->tm_mon < 12 ? fields->tm_mon : 0;
    char shortName[4] = { 0, 0, 0, 0 };
    switch (c) {
    case 'a': for (int i = 0; i < 3; i++) shortName[i] = weekdays[wday][i]; put(shortName); break;
    case 'A': put(weekdays[wday]); break;
    case 'b': case 'h': for (int i = 0; i < 3; i++) shortName[i] = months[mon][i]; put(shortName); break;
    case 'B': put(months[mon]); break;
    case 'c': run("%a %b %e %H:%M:%S %Y", fields); break;
    case 'C': number(year / 100, 2, '0'); break;
    case 'd': number(fields->tm_mday, 2, '0'); break;
    case 'D': case 'x': run("%m/%d/%y", fields); break;
    case 'e': number(fields->tm_mday, 2, ' '); break;
    case 'F': run("%Y-%m-%d", fields); break;
    case 'g': isoWeek(fields, isoYear); number(isoYear % 100, 2, '0'); break;
    case 'G': isoWeek(fields, isoYear); number(isoYear, 1, '0'); break;
    case 'H': number(fields->tm_hour, 2, '0'); break;
    case 'I': number(fields->tm_hour % 12 == 0 ? 12 : fields->tm_hour % 12, 2, '0'); break;
    case 'j': number(fields->tm_yday + 1, 3, '0'); break;
    case 'm': number(fields->tm_mon + 1, 2, '0'); break;
    case 'M': number(fields->tm_min, 2, '0'); break;
    case 'n': put('\n'); break;
    case 'p': put(fields->tm_hour < 12 ? "AM" : "PM"); break;
    case 'r': run("%I:%M:%S %p", fields); break;
    case 'R': run("%H:%M", fields); break;
    case 'S': number(fields->tm_sec, 2, '0'); break;
    case 't': put('\t'); break;
    case 'T': case 'X': run("%H:%M:%S", fields); break;
    case 'u': number(wday == 0 ? 7 : wday, 1, '0'); break;
    case 'U': number((fields->tm_yday + 7 - wday) / 7, 2, '0'); break;
    case 'V': number(isoWeek(fields, isoYear), 2, '0'); break;
    case 'w': number(wday, 1, '0'); break;
    case 'W': number((fields->tm_yday + 7 - (wday + 6) % 7) / 7, 2, '0'); break;
    case 'y': number(year % 100 < 0 ? year % 100 + 100 : year % 100, 2, '0'); break;
    case 'Y': number(year, 1, '0'); break;
    case 'z': put('+'); number(0, 4, '0'); break;
    case 'Z': put(fields->tm_zone ? fields->tm_zone : ""); break;
    case '%': put('%'); break;
    default: put('%'); put(c); break;
    }
}

}  // namespace rts6x
