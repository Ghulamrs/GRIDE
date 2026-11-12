// Spec: Microsoft C runtime, _putenv on a target whose environment is the host's to read and nobody's to write:
// the program's own entries, kept here, come before the host's for getenv.
#ifndef RTS6X_ENVIRONMENT_H
#define RTS6X_ENVIRONMENT_H

namespace rts6x {

class Environment {
public:
    // "NAME=value" set, "NAME=" removed: 0, or -1 with errno.
    static int put(const char *entry);
    // The program's own say on name: 1 and *value its value, -1 when it removed name, 0 when it said nothing.
    static int lookup(const char *name, const char **value);
private:
    static char **entries_;
    static unsigned count_, capacity_;
};

}  // namespace rts6x

#endif