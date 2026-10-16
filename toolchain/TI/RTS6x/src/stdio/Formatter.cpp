// Spec: ISO C 7.19.6.1/3-6: ordinary characters copied (a narrow format's a run at a time), `%%` a
// percent sign, each conversion specification replaced by its argument converted, in a field padded
// to the width - left with spaces, or zeros where 0 is given and allowed, right where - is given.

#include "Formatter.h"

namespace rts6x {

Formatter::Formatter(OutputSink &out, va_list *args) : out_(out), args_(args), failed_(false)
{
}

bool Formatter::narrow(unsigned wide, char &byte)
{
    if (wide > 0xFF) { failed_ = true; return false; }
    byte = (char)wide;
    return true;
}

int Formatter::run(FormatText text)
{
    while (!failed_) {
        if (const char *p = text.narrowText()) {
            const char *q = p;
            char c;
            while ((c = *q) != 0 && c != '%') q++;
            if (q != p) out_.put(p, (size_t)(q - p));
            if (c == 0) break;
            if (q[1] == '%') {
                text.skip((unsigned)(q - p) + 2);
                out_.put('%');
                continue;
            }
            const char *after = q + 1;
            FormatSpec spec;
            bool ok = spec.parse(after, args_);
            text.skip((unsigned)(after - p));
            if (!ok) { failed_ = true; break; }
            convert(spec);
            continue;
        } else {
            if (text.done()) break;
            unsigned c = text.at();
            text.next();
            char byte;
            if (c != '%') {
                if (narrow(c, byte)) out_.put(byte);
                continue;
            }
            if (text.at() == '%') {
                text.next();
                out_.put('%');
                continue;
            }
        }
        FormatSpec spec;
        if (!spec.parse(text, args_)) { failed_ = true; break; }
        convert(spec);
    }
    int n = out_.finish();
    return failed_ ? -1 : n;
}

void Formatter::convert(const FormatSpec &spec)
{
    switch (spec.conversion()) {
    case 'd': case 'i': case 'o': case 'u': case 'x': case 'X': integer(spec); break;
    case 'c': character(spec); break;
    case 's': string(spec); break;
    case 'p': pointer(spec); break;
    case 'n': count(spec); break;
    default: floating(spec); break;
    }
}

size_t Formatter::signPrefix(const FormatSpec &spec, bool negative, char *prefix)
{
    if (negative) { prefix[0] = '-'; return 1; }
    if (spec.plusSign()) { prefix[0] = '+'; return 1; }
    if (spec.spaceSign()) { prefix[0] = ' '; return 1; }
    return 0;
}

size_t Formatter::open(const FormatSpec &spec, const char *prefix, size_t prefixLength, size_t bodyLength, bool zeros)
{
    size_t used = prefixLength + bodyLength;
    size_t padding = spec.width() > 0 && (size_t)spec.width() > used ? (size_t)spec.width() - used : 0;
    if (spec.leftAlign()) {
        out_.put(prefix, prefixLength);
    } else if (zeros && spec.zeroPad()) {
        out_.put(prefix, prefixLength);
        out_.repeat('0', padding);
        padding = 0;
    } else {
        out_.repeat(' ', padding);
        out_.put(prefix, prefixLength);
        padding = 0;
    }
    return padding;
}

void Formatter::close(const FormatSpec &spec, size_t padding)
{
    if (spec.leftAlign()) out_.repeat(' ', padding);
}

}  // namespace rts6x
