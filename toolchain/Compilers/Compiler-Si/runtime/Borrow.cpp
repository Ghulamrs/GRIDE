
#include "Borrow.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace shm {

CText::CText(const ShmArray *text) {
    const Array *array = reinterpret_cast<const Array *>(text);
    const size_t length = static_cast<size_t>(textLength(array));
    text_ = static_cast<char *>(std::malloc(length + 1));
    if (!text_) fail("Out of memory");
    if (length > 0) std::memcpy(text_, array->chars(), length);
    text_[length] = '\0';
}

CText::~CText() { std::free(text_); }

void copyText(ShmArray *out, const char *text) {
    Array *array = reinterpret_cast<Array *>(out);
    if (!array || array->count < 1) return;
    const size_t capacity = static_cast<size_t>(array->count) - 1;
    const size_t length = text ? std::strlen(text) : 0;
    const size_t n = length < capacity ? length : capacity;
    if (n > 0) std::memcpy(array->chars(), text, n);
    std::memset(array->chars() + n, 0, static_cast<size_t>(array->count) - n);
}

void failed(const char *name) {
    char message[160];
    const int code = errno;
    std::snprintf(message, sizeof message, "%s: %s", name, code ? std::strerror(code) : "failed");
    fail(message);
}

int32_t toInt(const char *name, long long value) {
    if (value >= -2147483647LL - 1 && value <= 2147483647LL) return static_cast<int32_t>(value);
    char message[120];
    std::snprintf(message, sizeof message, "%s: %lld is past int's range", name, value);
    fail(message);
}

size_t toSize(const char *name, int32_t value) {
    if (value >= 0) return static_cast<size_t>(value);
    char message[120];
    std::snprintf(message, sizeof message, "%s: a size of %d", name, static_cast<int>(value));
    fail(message);
}

}
