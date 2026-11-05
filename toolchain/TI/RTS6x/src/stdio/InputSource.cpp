// Spec: ISO C 7.19.6.2/9 and 7.19.6.7/2 - reaching the end of sscanf's string is reaching the end of
// the file; a stream's characters come through its own buffer, which takes the pushed-back one.

#include "InputSource.h"
#include "Stream.h"

#if defined(_WIN32)
// grt (c\wstdio.cpp): a stream's next wide character, the stream locked by the caller, or EOF; one given back
extern "C" int __grt_get_wide(FILE *f);
extern "C" void __grt_unget_wide(int c, FILE *f);
#endif

namespace rts6x {

int InputSource::get()
{
    int c;
#if defined(_WIN32)
    if (wide_) {
        c = *wide_ ? (int)*wide_++ : EOF;
        if (c != EOF) count_++;
        return c;
    }
    if (wideStream_) {
        c = __grt_get_wide(stream_);
        if (c == EOF) failed_ = Stream(stream_).failed();
        else count_++;
        return c;
    }
#endif
    if (text_) {
        c = *text_ ? (unsigned char)*text_++ : EOF;
    } else {
        Stream in(stream_);
        c = in.get();
        if (c == EOF) failed_ = in.failed();
    }
    if (c != EOF) count_++;
    return c;
}

void InputSource::unget(int c)
{
    if (c == EOF) return;
    count_--;
#if defined(_WIN32)
    if (wide_) { wide_--; return; }
    if (wideStream_) { __grt_unget_wide(c, stream_); return; }
#endif
    if (text_) {
        text_--;
    } else {
        Stream in(stream_);
        in.unget(c);
    }
}

}  // namespace rts6x
