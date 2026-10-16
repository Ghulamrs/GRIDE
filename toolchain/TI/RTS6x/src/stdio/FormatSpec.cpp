// Spec: ISO C 7.19.6.1/4-8: flags `- + space # 0`; width as digits or `*` (negative: `-` and its
// magnitude); precision as `.` then digits or `*` (negative: none); `hh h l ll j z t L`. Parsed
// from narrow text; a wide format's specification is ASCII or none, so it is narrowed first.

#include "FormatSpec.h"

namespace rts6x {

const unsigned char FormatSpec::conversions_[7] = { 0x71, 0x00, 0x80, 0x00, 0x7D, 0xE1, 0x94 };

FormatSpec::FormatSpec()
    : leftAlign_(false), plusSign_(false), spaceSign_(false), alternate_(false), zeroPad_(false),
      width_(0), precision_(-1), length_(Plain), conversion_(0)
{
}

const char *FormatSpec::digits(const char *q, int &value)
{
    int n = 0;
    for (char c = *q; c >= '0' && c <= '9'; c = *++q) n = n * 10 + (c - '0');
    value = n;
    return q;
}

bool FormatSpec::parse(FormatText &text, va_list *args)
{
    const char *p = text.narrowText();
    char narrowed[64];
    if (p == 0) {
        unsigned n = 0;
        for (unsigned c; n < sizeof narrowed - 1 && (c = text.at(n)) != 0 && c < 0x80; n++) {
            narrowed[n] = (char)c;
            if (isConversion((char)c)) { n++; break; }
        }
        narrowed[n] = 0;
        p = narrowed;
    }
    const char *start = p;
    bool ok = parse(p, args);
    text.skip((unsigned)(p - start));
    return ok;
}

bool FormatSpec::parse(const char *&p, va_list *args)
{
    const char *q = p;
    for (;; q++) {
        char c = *q;
        if (c > '0') break;
        if (c == '-') leftAlign_ = true;
        else if (c == '+') plusSign_ = true;
        else if (c == ' ') spaceSign_ = true;
        else if (c == '#') alternate_ = true;
        else if (c == '0') zeroPad_ = true;
        else break;
    }
    if (*q == '*') {
        q++;
        width_ = va_arg(*args, int);
        if (width_ < 0) { leftAlign_ = true; width_ = -width_; }
    } else {
        q = digits(q, width_);
    }
    if (*q == '.') {
        q++;
        if (*q == '*') {
            q++;
            precision_ = va_arg(*args, int);
            if (precision_ < 0) precision_ = -1;
        } else {
            q = digits(q, precision_);
        }
    }
    switch (*q) {
    case 'h': q++; length_ = Short; if (*q == 'h') { q++; length_ = Char; } break;
    case 'l': q++; length_ = Long; if (*q == 'l') { q++; length_ = LongLong; } break;
    case 'j': q++; length_ = IntMax; break;
    case 'z': q++; length_ = Size; break;
    case 't': q++; length_ = PtrDiff; break;
    case 'L': q++; length_ = LongDouble; break;
    default: break;
    }
    char c = *q;
    p = q + 1;
    if (!isConversion(c)) { p = q; return false; }
    conversion_ = c;
    return true;
}

}  // namespace rts6x
