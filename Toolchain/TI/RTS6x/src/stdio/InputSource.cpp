// Spec: ISO C 7.19.6.2/9 and 7.19.6.7/2 - reaching the end of sscanf's string is reaching the end of
// the file; a stream's characters come through its own buffer, which takes the pushed-back one.

#include "InputSource.h"
#include "Stream.h"

namespace rts6x {

int InputSource::get()
{
    int c;
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
    if (text_) {
        text_--;
    } else {
        Stream in(stream_);
        in.unget(c);
    }
}

}  // namespace rts6x
