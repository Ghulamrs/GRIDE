// Spec: ISO C 7.19.6.1/14 and 7.19.6.6/2 - what the printf family counts and where its output goes:
// a host file descriptor, staged and written through CioChannel, or memory, with sprintf's NUL.
// One class for both, so the formatter writes characters and never knows which.
#ifndef RTS6X_OUTPUT_SINK_H
#define RTS6X_OUTPUT_SINK_H

#include <stddef.h>

namespace rts6x {

class OutputSink {
public:
    // Into memory at dst; capacity is how many bytes may be written, the NUL included.
    OutputSink(char *dst, size_t capacity);
    // To the host file descriptor fd; a negative one is a stream refusing to write, and finish() says -1.
    explicit OutputSink(int fd);

    // A byte goes where next_ points while there is room; past it, overflow() stages or counts it.
    void put(char c) { if (next_ != end_) *next_++ = c; else overflow(c); }
    void put(const char *s, size_t n) { if (n != 0) putRun(s, n); }
    void repeat(char c, size_t n) { if (n != 0) repeatRun(c, n); }
    // A string up to its NUL.
    void put(const char *s);
    // Staged bytes written or the NUL stored; the count, or -1 if a write was refused.
    int finish();

    size_t count() const { return (size_t)(next_ - start_) + passed_; }

private:
    enum { Staging = 128 };
    void overflow(char c);
    void putRun(const char *s, size_t n);
    void repeatRun(char c, size_t n);

    // Memory: start_ is dst and end_ the last byte before the NUL's. A descriptor: both bound
    // staging_. passed_ counts the bytes no longer between start_ and next_ (written or dropped).
    char *start_;
    char *next_;
    char *end_;
    size_t passed_;
    size_t capacity_;
    int fd_;
    bool failed_;
    char staging_[Staging];
};

}  // namespace rts6x

#endif
