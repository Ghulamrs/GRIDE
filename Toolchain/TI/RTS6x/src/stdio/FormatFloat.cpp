// Spec: ISO C 7.19.6.1/8: f F [-]ddd.ddd, e E [-]d.ddde+dd, g G the shorter of the two by the
// exponent with trailing zeros dropped unless #, a A [-]0xh.hhhp+d; precision 6 by default (exact
// for a); inf and nan for infinities and NaNs. long double is double on the C6000 (SPRAB89B 2.1).
#include "Formatter.h"
#include "DecimalDigits.h"

namespace rts6x {

size_t Formatter::exponentText(int exponent, unsigned minDigits, char *text)
{
    text[0] = exponent < 0 ? '-' : '+';
    unsigned e = (unsigned)(exponent < 0 ? -exponent : exponent);
    char digits[8];
    unsigned n = 0;
    do { digits[n++] = (char)('0' + e % 10); e /= 10; } while (e != 0);
    while (n < minDigits) digits[n++] = '0';
    for (unsigned i = 0; i < n; i++) text[1 + i] = digits[n - 1 - i];
    return 1 + n;
}

void Formatter::floating(const FormatSpec &spec)
{
    double v = va_arg(*args_, double);
    union { double d; unsigned w[2]; } bits;
    bits.d = v;
    unsigned hi = bits.w[1] & 0x7FFFFFFFu, lo = bits.w[0];
    bool negative = (bits.w[1] >> 31) != 0;
    char prefix[4];

    if ((hi >> 20) == 0x7FF) {
        bool nan = (hi & 0xFFFFFu) != 0 || lo != 0;
        size_t p = signPrefix(spec, negative && !nan, prefix);
        const char *t = nan ? (spec.upper() ? "NAN" : "nan") : (spec.upper() ? "INF" : "inf");
        size_t padding = open(spec, prefix, p, 3, false);
        out_.put(t, 3);
        close(spec, padding);
        return;
    }
    size_t p = signPrefix(spec, negative, prefix);
    char c = (char)(spec.conversion() | 0x20);
    if (c == 'a') { hexadecimal(spec, prefix, p, hi, lo); return; }

    int precision = spec.precision() < 0 ? 6 : spec.precision();
    DecimalDigits d(v);
    if (c == 'f') {
        d.round(d.point() + precision);
        fixed(spec, prefix, p, d, precision);
        return;
    }
    if (c == 'e') {
        if (!d.zero()) d.round(precision + 1);
        scientific(spec, prefix, p, d, precision, d.zero() ? 0 : d.point() - 1);
        return;
    }
    // g: the exponent the e style would have decides; then trailing zeros go, unless #.
    if (precision == 0) precision = 1;
    int x = d.zero() ? 0 : d.pointAfterRound(precision) - 1;
    if (precision > x && x >= -4) {
        int fraction = precision - 1 - x;
        d.round(d.point() + fraction);
        int after = d.count() - d.point();
        if (!spec.alternate() && fraction > after) fraction = after < 0 ? 0 : after;
        fixed(spec, prefix, p, d, fraction);
    } else {
        int fraction = precision - 1;
        d.round(precision);
        if (!spec.alternate() && fraction > d.count() - 1) fraction = d.count() > 0 ? d.count() - 1 : 0;
        scientific(spec, prefix, p, d, fraction, x);
    }
}

void Formatter::fixed(const FormatSpec &spec, const char *prefix, size_t prefixLength, const DecimalDigits &d, int fraction)
{
    int point = d.point();
    size_t whole = point > 0 ? (size_t)point : 1;
    bool dot = fraction > 0 || spec.alternate();
    size_t padding = open(spec, prefix, prefixLength, whole + (dot ? 1 : 0) + (size_t)fraction, true);
    if (point > 0) {
        for (int i = 0; i < point; i++) out_.put(d.digit(i));
    } else {
        out_.put('0');
    }
    if (dot) out_.put('.');
    for (int j = 0; j < fraction; j++) out_.put(d.digit(point + j));
    close(spec, padding);
}

void Formatter::scientific(const FormatSpec &spec, const char *prefix, size_t prefixLength, const DecimalDigits &d,
                           int fraction, int exponent)
{
    char exp[8];
    size_t en = exponentText(exponent, 2, exp);
    bool dot = fraction > 0 || spec.alternate();
    size_t padding = open(spec, prefix, prefixLength, 1 + (dot ? 1 : 0) + (size_t)fraction + 1 + en, true);
    out_.put(d.digit(0));
    if (dot) out_.put('.');
    for (int j = 0; j < fraction; j++) out_.put(d.digit(1 + j));
    out_.put(spec.upper() ? 'E' : 'e');
    out_.put(exp, en);
    close(spec, padding);
}

// a A: one hexadecimal digit before the point - 1 for a normal number, and a subnormal shifted
// until it has one - then the fraction's 13 hexadecimal digits, rounded to the precision.
void Formatter::hexadecimal(const FormatSpec &spec, const char *prefix, size_t prefixLength, unsigned hi, unsigned lo)
{
    unsigned top = hi & 0xFFFFFu;
    int biased = (int)(hi >> 20);
    unsigned lead = 1;
    int exponent = biased - 1023;
    if (biased == 0 && top == 0 && lo == 0) {
        lead = 0;
        exponent = 0;
    } else if (biased == 0) {
        exponent = -1022;
        while ((top & 0x100000u) == 0) { top = (top << 1) | (lo >> 31); lo <<= 1; exponent--; }
        top &= 0xFFFFFu;
    }
    unsigned nibble[13];
    for (int i = 0; i < 5; i++) nibble[i] = (top >> (16 - 4 * i)) & 15u;
    for (int i = 0; i < 8; i++) nibble[5 + i] = (lo >> (28 - 4 * i)) & 15u;

    int n = spec.precision();
    if (n < 0) {
        n = 13;
        while (n > 0 && nibble[n - 1] == 0) n--;
    } else if (n < 13) {
        bool rest = false;
        for (int k = n + 1; k < 13; k++) rest = rest || nibble[k] != 0;
        unsigned before = n > 0 ? nibble[n - 1] : lead;
        if (nibble[n] > 8 || (nibble[n] == 8 && (rest || (before & 1) != 0))) {
            int k = n - 1;
            while (k >= 0 && nibble[k] == 15) nibble[k--] = 0;
            if (k >= 0) nibble[k]++;
            else lead++;
        }
    }
    const char *glyphs = spec.upper() ? "0123456789ABCDEF" : "0123456789abcdef";
    char full[6];
    for (size_t i = 0; i < prefixLength; i++) full[i] = prefix[i];
    full[prefixLength] = '0';
    full[prefixLength + 1] = spec.upper() ? 'X' : 'x';
    char exp[8];
    size_t en = exponentText(exponent, 1, exp);
    bool dot = n > 0 || spec.alternate();
    size_t padding = open(spec, full, prefixLength + 2, 1 + (dot ? 1 : 0) + (size_t)n + 1 + en, true);
    out_.put(glyphs[lead]);
    if (dot) out_.put('.');
    for (int i = 0; i < n; i++) out_.put(i < 13 ? glyphs[nibble[i]] : '0');
    out_.put(spec.upper() ? 'P' : 'p');
    out_.put(exp, en);
    close(spec, padding);
}

}  // namespace rts6x
