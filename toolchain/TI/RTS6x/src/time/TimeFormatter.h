// Spec: ISO C 7.23.3.5 (strftime's conversions, in the "C" locale) - a struct tm written into a
// buffer of fixed size under a format. Every conversion C99 lists, the E and O modifiers read and
// ignored as the "C" locale allows.
#ifndef RTS6X_TIME_FORMATTER_H
#define RTS6X_TIME_FORMATTER_H

#include <stddef.h>
#include <time.h>

namespace rts6x {

class TimeFormatter {
public:
    TimeFormatter(char *dst, size_t capacity) : dst_(dst), capacity_(capacity), count_(0), overflow_(false) {}
    // The count written without the NUL, or 0 when it did not fit.
    size_t run(const char *format, const tm *fields);

private:
    void put(char c);
    void put(const char *s) { while (*s) put(*s++); }
    // v in at least width digits, padded with pad.
    void number(long v, int width, char pad);
    void conversion(char c, const tm *fields);
    // ISO 8601: the week-based year and its week number (%G, %g, %V).
    static int isoWeek(const tm *fields, long &year);

    char *dst_;
    size_t capacity_;
    size_t count_;
    bool overflow_;
};

}  // namespace rts6x

#endif
