// Spec: ISO C 7.19.6.2/9 and 7.19.6.7 - where the scanf family reads: a stream (fscanf, scanf) or a
// string (sscanf), one character at a time with at most one pushed back, and the count of
// characters consumed, which %n reports.
#ifndef RTS6X_INPUT_SOURCE_H
#define RTS6X_INPUT_SOURCE_H

#include <stdio.h>

namespace rts6x {

class InputSource {
public:
    explicit InputSource(const char *s) : text_(s), stream_(0), count_(0), failed_(false) {}
    explicit InputSource(FILE *f) : text_(0), stream_(f), count_(0), failed_(false) {}

    // The next character as an unsigned char, or EOF at the end of the input or on a read error.
    int get();
    // c, the character get() last answered, given back to be read again.
    void unget(int c);
    int count() const { return count_; }
    // Whether the last EOF was a read error rather than the end of the input.
    bool failed() const { return failed_; }
    // Whether the input is at its end: the next character, read and given back, is EOF.
    bool endReached() { int c = get(); unget(c); return c == EOF; }

private:
    const char *text_;
    FILE *stream_;
    int count_;
    bool failed_;
};

}  // namespace rts6x

#endif
