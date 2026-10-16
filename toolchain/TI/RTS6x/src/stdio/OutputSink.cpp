// Spec: ISO C 7.19.6.1/14 (the count is of characters transmitted) and 7.19.6.6/2 (sprintf ends
// what it writes with a NUL) - the two destinations of OutputSink.h. A run of bytes that fits the
// room left goes in one memcpy or memset; one that does not, a byte at a time through overflow().
#include "OutputSink.h"
#include "../host/CioChannel.h"
#include <string.h>

namespace rts6x {

OutputSink::OutputSink(char *dst, size_t capacity)
    : start_(dst), next_(dst), end_(dst), passed_(0), capacity_(capacity), fd_(-1), failed_(false)
{
    if (capacity != 0) {
        size_t room = capacity - 1, most = (size_t)-1 - (size_t)dst;
        end_ = dst + (room < most ? room : most);
    }
}

OutputSink::OutputSink(int fd)
    : start_(staging_), next_(staging_), end_(staging_ + Staging), passed_(0), capacity_(0), fd_(fd),
      failed_(fd < 0)
{
}

void OutputSink::overflow(char c)
{
    if (fd_ < 0) { passed_++; return; }
    unsigned staged = (unsigned)(next_ - start_);
    if (CioChannel::write(fd_, staging_, staged) < 0) failed_ = true;
    passed_ += staged;
    next_ = staging_;
    *next_++ = c;
}

void OutputSink::putRun(const char *s, size_t n)
{
    char *next = next_;
    if (n <= (size_t)(end_ - next)) {
        memcpy(next, s, n);
        next_ = next + n;
        return;
    }
    for (size_t i = 0; i < n; i++) put(s[i]);
}

void OutputSink::put(const char *s)
{
    put(s, strlen(s));
}

void OutputSink::repeatRun(char c, size_t n)
{
    char *next = next_;
    if (n <= (size_t)(end_ - next)) {
        memset(next, c, n);
        next_ = next + n;
        return;
    }
    for (size_t i = 0; i < n; i++) put(c);
}

int OutputSink::finish()
{
    if (fd_ >= 0) {
        unsigned staged = (unsigned)(next_ - start_);
        if (staged != 0 && CioChannel::write(fd_, staging_, staged) < 0) failed_ = true;
        passed_ += staged;
        next_ = staging_;
    } else if (capacity_ != 0) {
        *next_ = '\0';
    }
    return failed_ ? -1 : (int)count();
}

}  // namespace rts6x
