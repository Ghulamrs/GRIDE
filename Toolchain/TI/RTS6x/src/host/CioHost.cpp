// Spec: the C$$IO$$ host channel (sim6747 src/C6xHost.cpp): unlink 0xF5 and rename 0xF7 (paths as
// data, 0 answered for success), getenv 0xF6 (the value answered as data, empty if unset), the
// time 0xF8 and the cycle count 0xF9 (32 bits at the answer's first parameter).

#include "CioChannel.h"

namespace rts6x {

int CioChannel::unlink(const char *path)
{
    unsigned char params[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    requestText(Unlink, params, path, 0);
    return answer16();
}

int CioChannel::rename(const char *from, const char *to)
{
    unsigned char params[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    requestText(Rename, params, from, to);
    return answer16();
}

bool CioChannel::environment(const char *name, char *value, unsigned capacity)
{
    unsigned char params[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    requestText(Getenv, params, name, 0);
    // An unset name is answered with an empty string, as is an empty value: both are "no value".
    unsigned length = (unsigned)get32(_CIOBUF_);
    if (length <= 1) return false;
    unsigned i = 0;
    for (; i + 1 < capacity && i < length && _CIOBUF_[ReplyHeader + i]; i++) value[i] = (char)_CIOBUF_[ReplyHeader + i];
    value[i] = 0;
    return true;
}

unsigned long CioChannel::hostTime()
{
    unsigned char params[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    request(GetTime, params, 0, 0);
    return get32(_CIOBUF_ + 4);
}

unsigned long CioChannel::cycles()
{
    unsigned char params[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    request(GetClock, params, 0, 0);
    return get32(_CIOBUF_ + 4);
}

}  // namespace rts6x
