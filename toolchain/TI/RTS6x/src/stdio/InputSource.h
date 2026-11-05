// Spec: ISO C 7.19.6.2/9 and 7.19.6.7 - where the scanf family reads: a stream (fscanf, scanf) or a
// string (sscanf), one character at a time with at most one pushed back, and the count of
// characters consumed, which %n reports.
#ifndef RTS6X_INPUT_SOURCE_H
#define RTS6X_INPUT_SOURCE_H

#include <stdio.h>

namespace rts6x {

class InputSource {
public:
#if defined(_WIN32)
    explicit InputSource(const char *s) : text_(s), stream_(0), count_(0), failed_(false), wide_(0), wideStream_(false) {}
    explicit InputSource(FILE *f) : text_(0), stream_(f), count_(0), failed_(false), wide_(0), wideStream_(false) {}
    // grt: the wscanf family's sources, a wide string (swscanf) or a stream read a wide character at a time
    // (fwscanf, through grt's __grt_get_wide and __grt_unget_wide); a character is its code unit's value
    explicit InputSource(const wchar_t *s) : text_(0), stream_(0), count_(0), failed_(false), wide_(s), wideStream_(false) {}
    InputSource(FILE *f, bool wide) : text_(0), stream_(f), count_(0), failed_(false), wide_(0), wideStream_(wide) {}
#else
    explicit InputSource(const char *s) : text_(s), stream_(0), count_(0), failed_(false) {}
    explicit InputSource(FILE *f) : text_(0), stream_(f), count_(0), failed_(false) {}
#endif

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
#if defined(_WIN32)
    const wchar_t *wide_;
    bool wideStream_;
#endif
};

}  // namespace rts6x

#endif
