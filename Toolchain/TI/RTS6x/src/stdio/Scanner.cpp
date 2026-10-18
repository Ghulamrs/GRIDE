// Spec: ISO C 7.19.6.2/3-14 - a white-space directive reads white space; an ordinary character
// must match; a conversion skips white space first except for [, c and n. A matching failure stops
// the scan; an input failure before any conversion completed answers EOF.

#include "Scanner.h"
#include "../ctype/CharacterClass.h"

namespace rts6x {

void Scanner::skipSpace()
{
    int c;
    do c = in_.get(); while (c != EOF && CharacterClass::space(c));
    in_.unget(c);
}

int Scanner::run(const char *format)
{
    while (*format) {
        unsigned char f = (unsigned char)*format;
        if (CharacterClass::space(f)) {
            while (CharacterClass::space((unsigned char)*format)) format++;
            skipSpace();
            continue;
        }
        if (f != '%' || format[1] == '%') {
            if (f == '%') {
                skipSpace();
                format++;
            }
            int c = in_.get();
            if (c != (unsigned char)*format) {
                in_.unget(c);
                if (c == EOF && !converted_) return EOF;
                return assigned_;
            }
            format++;
            continue;
        }
        format++;
        bool assign = true;
        if (*format == '*') { assign = false; format++; }
        int width = 0;
        while (CharacterClass::digit((unsigned char)*format)) width = width * 10 + (*format++ - '0');
        Length length = Default;
        switch (*format) {
        case 'h': length = format[1] == 'h' ? Char : Short; format += format[1] == 'h' ? 2 : 1; break;
        case 'l': length = format[1] == 'l' ? LongLong : Long; format += format[1] == 'l' ? 2 : 1; break;
        case 'j': case 'q': length = LongLong; format++; break;
        case 'L': length = LongDouble; format++; break;
        case 'z': case 't': length = Size; format++; break;
        default: break;
        }
        char conversion = *format++;
        if (!conversion) break;
        bool input = false;
        if (!convert(conversion, assign, width, length, format, input))
            return input && !converted_ ? EOF : assigned_;
        converted_ = true;
    }
    return assigned_;
}

bool Scanner::convert(char conversion, bool assign, int width, Length length, const char *&format, bool &input)
{
    switch (conversion) {
    case 'n':
        if (assign) {
            int n = in_.count();
            switch (length) {
            case Char: *va_arg(*args_, signed char *) = (signed char)n; break;
            case Short: *va_arg(*args_, short *) = (short)n; break;
            case LongLong: *va_arg(*args_, long long *) = n; break;
            default: *va_arg(*args_, int *) = n; break;
            }
        }
        return true;
    case 'c':
        return characters(assign, width, length, input);
    case '[':
        return scanset(assign, width, length, format, input);
    default:
        break;
    }
    skipSpace();
    switch (conversion) {
    case 'd': case 'i': case 'o': case 'u': case 'x': case 'X': case 'p':
        return integer(conversion, assign, width, length, input);
    case 'a': case 'A': case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
        return floating(assign, width, length, input);
    case 's':
        return word(assign, width, length, input);
    default:
        return false;
    }
}

}  // namespace rts6x
