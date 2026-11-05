
#pragma once

#include "Internal.h"

#include <cstddef>

// What runtime\wrappers\ stands on: where Shalimar's types meet C's (docs\Runtime_Design.md 14.3).
namespace shm {

// A Shalimar text as C wants it, for the length of a call: its characters to the first NUL, NUL-terminated
// whatever the array's capacity.
class CText {
public:
    explicit CText(const ShmArray *text);
    ~CText();

    operator const char *() const { return text_; }

private:
    CText(const CText &);
    CText &operator=(const CText &);

    char *text_;
};

// A C string into the text a call was given, as an assignment fits it: cut at its capacity. Null is the empty text.
void copyText(ShmArray *out, const char *text);

// "name: <strerror>", which ends the program.
[[noreturn]] void failed(const char *name);

// A C result into an int, and an int into a size: a value the other cannot hold ends the program.
int32_t toInt(const char *name, long long value);
size_t toSize(const char *name, int32_t value);

}
