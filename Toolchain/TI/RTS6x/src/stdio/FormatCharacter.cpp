// Spec: ISO C 7.19.6.1/8: c the int as an unsigned char, lc a wint_t as a multibyte character;
// s the bytes up to a NUL or the precision, ls wide characters converted the same way; n stores
// the count so far. 7.24.2.1/8 gives wprintf the same arguments. "C" locale: a byte per character.

#include "Formatter.h"
#include <string.h>

namespace rts6x {

void Formatter::character(const FormatSpec &spec)
{
    char byte;
    if (spec.length() == FormatSpec::Long) {
        if (!narrow((unsigned)va_arg(*args_, int), byte)) return;
    } else {
        byte = (char)(unsigned char)va_arg(*args_, int);
    }
    size_t padding = open(spec, "", 0, 1, false);
    out_.put(byte);
    close(spec, padding);
}

void Formatter::string(const FormatSpec &spec)
{
    size_t limit = spec.precision() < 0 ? (size_t)-1 : (size_t)spec.precision();
    if (spec.length() == FormatSpec::Long) {
        const wchar_t *s = va_arg(*args_, const wchar_t *);
        if (s == 0) s = L"(null)";
        size_t n = 0;
        while (n < limit && s[n] != 0) {
            if (s[n] > 0xFF) { failed_ = true; return; }
            n++;
        }
        size_t padding = open(spec, "", 0, n, false);
        for (size_t i = 0; i < n; i++) out_.put((char)s[i]);
        close(spec, padding);
        return;
    }
    const char *s = va_arg(*args_, const char *);
    if (s == 0) s = "(null)";
    size_t n = 0;
    if (limit == (size_t)-1) n = strlen(s);
    else while (n < limit && s[n] != 0) n++;
    size_t padding = open(spec, "", 0, n, false);
    out_.put(s, n);
    close(spec, padding);
}

void Formatter::count(const FormatSpec &spec)
{
    size_t n = out_.count();
    switch (spec.length()) {
    case FormatSpec::Char: *va_arg(*args_, signed char *) = (signed char)n; break;
    case FormatSpec::Short: *va_arg(*args_, short *) = (short)n; break;
    case FormatSpec::Long: *va_arg(*args_, long *) = (long)n; break;
    case FormatSpec::LongLong: case FormatSpec::IntMax: *va_arg(*args_, long long *) = (long long)n; break;
    default: *va_arg(*args_, int *) = (int)n; break;
    }
}

}  // namespace rts6x
