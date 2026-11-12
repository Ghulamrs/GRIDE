// Spec: Microsoft C runtime, _putenv - the entries a program put, each its own copy, names compared as Windows'
// are (case ignored), since the host whose environment getenv reads otherwise is Windows.

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "Environment.h"
#include "ErrorNumber.h"

namespace rts6x {

char **Environment::entries_ = 0;
unsigned Environment::count_ = 0, Environment::capacity_ = 0;

namespace {
bool sameName(const char *entry, const char *name, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char a = entry[i], b = name[i];
        if (a >= 'a' && a <= 'z') a = static_cast<char>(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = static_cast<char>(b - 'a' + 'A');
        if (a != b) return false;
    }
    return entry[n] == '=';
}
}

int Environment::put(const char *entry)
{
    size_t n = 0;
    while (entry[n] != 0 && entry[n] != '=') n++;
    if (n == 0 || entry[n] != '=') {
        errno = EINVAL;
        return -1;
    }
    char *copy = static_cast<char *>(malloc(strlen(entry) + 1));
    if (copy == 0) return -1;
    strcpy(copy, entry);
    for (unsigned i = 0; i < count_; i++)
        if (sameName(entries_[i], entry, n)) {
            entries_[i] = copy;                     // the old one not freed: getenv may have given it out
            return 0;
        }
    if (count_ == capacity_) {
        unsigned grown = capacity_ == 0 ? 8 : capacity_ * 2;
        char **more = static_cast<char **>(realloc(entries_, grown * sizeof(char *)));
        if (more == 0) {
            free(copy);
            return -1;
        }
        entries_ = more;
        capacity_ = grown;
    }
    entries_[count_++] = copy;
    return 0;
}

int Environment::lookup(const char *name, const char **value)
{
    size_t n = strlen(name);
    for (unsigned i = 0; i < count_; i++)
        if (sameName(entries_[i], name, n)) {
            const char *v = entries_[i] + n + 1;
            if (*v == 0) return -1;
            *value = v;
            return 1;
        }
    return 0;
}

}  // namespace rts6x