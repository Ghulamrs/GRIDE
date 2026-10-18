// Spec: Itanium C++ ABI 2.9.7 - the subobjects of a most derived object, found from its class's
// type_info: each base at its offset, a virtual base where the object's vtable says, a virtual base
// reached by several paths counted once, public when any path to it is public.
#ifndef RTS6X_SUBOBJECTS_H
#define RTS6X_SUBOBJECTS_H

#include "TypeInfo.h"

namespace rts6x {

class Subobjects {
public:
    enum { Capacity = 64 };
    struct Entry { const void *type; const char *address; bool isPublic; };

    // Every subobject of the object at address of type type, itself included (no constructor: static is data).
    void collect(const char *address, const TypeInfo &type);

    int count() const { return count_; }
    bool overflow() const { return overflow_; }
    const Entry &at(int i) const { return entries_[i]; }
    // Whether a subobject of type target lies at address, reached publicly.
    bool hasPublic(const TypeInfo &target, const char *address) const;

private:
    void walk(const char *address, const TypeInfo &type, bool isPublic);
    void add(const char *address, const void *type, bool isPublic);

    Entry entries_[Capacity];
    int count_;
    bool overflow_;
};

}  // namespace rts6x

#endif
