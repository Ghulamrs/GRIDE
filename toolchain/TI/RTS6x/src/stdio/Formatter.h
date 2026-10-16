// Spec: ISO C 7.19.6.1 (fprintf) and 7.24.2.1 (fwprintf) - the conversions of the printf family,
// one class for all of them: a format read narrow or wide, arguments from a va_list, characters to
// an OutputSink. Each conversion is a member of its own, in a source file of its own.
#ifndef RTS6X_FORMATTER_H
#define RTS6X_FORMATTER_H

#include <stdarg.h>
#include <stddef.h>
#include "FormatSpec.h"
#include "FormatText.h"
#include "OutputSink.h"

namespace rts6x {

class DecimalDigits;

class Formatter {
public:
    Formatter(OutputSink &out, va_list *args);
    // The whole format: the count written, or -1 after an encoding error or a refused write.
    int run(FormatText text);

private:
    // Formatter.cpp
    void convert(const FormatSpec &spec);
    // A field: spaces or zeros before, the prefix, the body the caller writes, spaces after.
    // open() returns the padding close() needs.
    size_t open(const FormatSpec &spec, const char *prefix, size_t prefixLength, size_t bodyLength, bool zeros);
    void close(const FormatSpec &spec, size_t padding);
    // A wide character's byte in the "C" locale; false, and the error noted, past 0xFF.
    bool narrow(unsigned wide, char &byte);
    static size_t signPrefix(const FormatSpec &spec, bool negative, char *prefix);

    // FormatInteger.cpp: d i o u x X, and p
    void integer(const FormatSpec &spec);
    void pointer(const FormatSpec &spec);
    void unsignedField(const FormatSpec &spec, unsigned hi, unsigned lo, bool negative, unsigned base);

    // FormatCharacter.cpp: c s n
    void character(const FormatSpec &spec);
    void string(const FormatSpec &spec);
    void count(const FormatSpec &spec);

    // FormatFloat.cpp: f F e E g G a A
    void floating(const FormatSpec &spec);
    void fixed(const FormatSpec &spec, const char *prefix, size_t prefixLength, const DecimalDigits &d, int fraction);
    void scientific(const FormatSpec &spec, const char *prefix, size_t prefixLength, const DecimalDigits &d,
                    int fraction, int exponent);
    void hexadecimal(const FormatSpec &spec, const char *prefix, size_t prefixLength, unsigned hi, unsigned lo);
    static size_t exponentText(int exponent, unsigned minDigits, char *text);

    OutputSink &out_;
    va_list *args_;
    bool failed_;
};

}  // namespace rts6x

#endif
