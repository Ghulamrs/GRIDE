// Print.cpp - what the printf engine (Printer.h) shares: the format's table of states, the float digits from
// RTS6x's formatter, and the two ends, a stream and memory.
#include <stdlib.h>
#include "Printer.h"
#include "stdio/Formatter.h"
#include "stdio/OutputSink.h"
#include "../StreamWin.h"

namespace grt {

// The class of each character from ' ' to 'z': 0 other, 1 %, 2 ., 3 *, 4 0, 5 1-9, 6 a flag (space # + -),
// 7 a length (F h I j l L N t T w z), 8 a conversion (A C E G S X Z a c d e f g i n o p s u x)
unsigned PrintFormat::classOf(char c)
{
    static const unsigned char classes[] = {
        6, 0, 0, 6, 0, 1, 0, 0, 0, 0, 3, 6, 0, 6, 2, 0,     // space ! " # $ % & ' ( ) * + , - . /
        4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0,     // 0-9 : ; < = > ?
        0, 8, 0, 8, 0, 8, 7, 8, 0, 7, 0, 0, 7, 0, 7, 0,     // @ A-O
        0, 0, 0, 8, 7, 0, 0, 0, 8, 0, 8, 0, 0, 0, 0, 0,     // P-Z [ \ ] ^ _
        0, 8, 0, 8, 8, 8, 8, 8, 7, 8, 7, 0, 7, 0, 8, 8,     // ` a-o
        8, 0, 0, 8, 7, 8, 0, 7, 8, 0, 7                     // p-z
    };
    unsigned char u = (unsigned char)c;
    return u < ' ' || u > 'z' ? 0 : classes[u - ' '];
}

// The next state for a character of each class (rows) in each state (columns: normal, %, flag, width, ., precision,
// length, conversion)
PrintFormat::State PrintFormat::nextState(char c, State previous)
{
    static const unsigned char next[9][8] = {
        { Normal, Normal,    Normal, Normal, Normal,    Normal,    Normal, Normal },   // other
        { Percent, Normal,   Normal, Normal, Normal,    Normal,    Normal, Percent },  // %
        { Normal, Dot,       Dot,    Dot,    Normal,    Normal,    Normal, Normal },   // .
        { Normal, Width,     Width,  Normal, Precision, Normal,    Normal, Normal },   // *
        { Normal, Flag,      Flag,   Width,  Precision, Precision, Normal, Normal },   // 0
        { Normal, Width,     Width,  Width,  Precision, Precision, Normal, Normal },   // 1-9
        { Normal, Flag,      Flag,   Normal, Normal,    Normal,    Normal, Normal },   // flag
        { Normal, Size,      Size,   Size,   Size,      Size,      Size,   Normal },   // length
        { Normal, Type,      Type,   Type,   Type,      Type,      Type,   Normal }    // conversion
    };
    return (State)next[classOf(c)][previous];
}

bool PrintFormat::number(const char *&p, int &value)
{
    long long v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p++ - '0');
        if (v > 0x7FFFFFFFLL) {
            while (*p >= '0' && *p <= '9') p++;
            return false;
        }
    }
    value = (int)v;
    return true;
}

bool PrintFormat::wideSpecifier(char conversion, Length length)
{
    if (length == L || length == W) return true;
    if (length == H || length == BigT) return false;
    return conversion != 'c' && conversion != 's';
}

unsigned PrintFormat::integerSize(Length length)
{
    switch (length) {
    case None: case L: case I32: return 4;
    case HH: return 1;
    case H: return 2;
    case LL: case J: case Z: case T: case I: case I64: return 8;
    default: return 0;
    }
}

int floatText(char conversion, bool alternate, int precision, va_list *args, char *buf, size_t capacity, char **heap)
{
    char spec[24], digits[12];
    int n = 0, k = 0;
    spec[n++] = '%';
    if (alternate) spec[n++] = '#';
    spec[n++] = '.';
    do digits[k++] = (char)('0' + precision % 10); while ((precision /= 10) != 0);
    while (k) spec[n++] = digits[--k];
    spec[n++] = conversion;
    spec[n] = 0;
    va_list again;
    va_copy(again, *args);
    *heap = 0;
    rts6x::OutputSink out(buf, capacity);
    int length = rts6x::Formatter(out, args).run(rts6x::FormatText(spec));
    if (length < 0 || (size_t)length < capacity) return length;
    char *big = (char *)malloc((size_t)length + 1);
    if (!big) return -1;
    rts6x::OutputSink all(big, (size_t)length + 1);
    rts6x::Formatter(all, &again).run(rts6x::FormatText(spec));
    *heap = big;
    return length;
}

void freeText(char *heap)
{
    if (heap) free(heap);
}

void StreamOut::drain()
{
    if (at_ != 0 && grt::put(f_, stage_, (unsigned)at_) < 0) failed_ = true;
    at_ = 0;
}

int printToStream(FILE *f, const char *format, va_list *args)
{
    StreamOut out(f);
    int n = Printer<StreamOut>(out, args).run(format);
    if (n < 0) f->flags |= rts6x::Stream::Failed;
    return n;
}

int printToMemory(char *dst, size_t capacity, bool snprintf, const char *format, va_list *args)
{
    rts6x::OutputSink out(dst, capacity);
    int n = Printer<rts6x::OutputSink>(out, args).run(format);
    if (n < 0 && snprintf && capacity != 0) dst[0] = 0;
    return n;
}

}  // namespace grt
