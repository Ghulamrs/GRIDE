// Spec: Itanium C++ ABI 2.9.7, ISO C++11 5.2.7/8 - from the most derived object (offset-to-top), the
// one dst subobject holding the src one as a public base (down-cast); else, src being public in the
// whole, dst if it is an unambiguous public base of it (cross-cast); else a null pointer.

#include "Subobjects.h"

namespace {

// The two lists are large; they live here rather than on the stack, the C6747 running one thread.
rts6x::Subobjects whole, within;

}  // namespace

extern "C" void *__dynamic_cast(const void *object, const void *source, const void *target, int hint)
{
    (void)hint;
    const char *const *vptr = *static_cast<const char *const *const *>(object);
    const char *top = static_cast<const char *>(object) + static_cast<const int *>(static_cast<const void *>(vptr))[-2];
    rts6x::TypeInfo src(source), dst(target), most(vptr[-1]);
    const char *at = static_cast<const char *>(object);

    whole.collect(top, most);
    if (whole.overflow()) return 0;
    const char *found = 0;
    int downs = 0;
    for (int i = 0; i < whole.count(); i++) {
        const rts6x::Subobjects::Entry &e = whole.at(i);
        if (!rts6x::TypeInfo(e.type).sameAs(dst)) continue;
        within.collect(e.address, dst);
        if (!within.overflow() && within.hasPublic(src, at) && e.address != found) {
            found = e.address;
            downs++;
        }
    }
    if (downs == 1) return const_cast<char *>(found);
    if (downs > 1 || !whole.hasPublic(src, at)) return 0;
    found = 0;
    for (int i = 0; i < whole.count(); i++) {
        const rts6x::Subobjects::Entry &e = whole.at(i);
        if (!rts6x::TypeInfo(e.type).sameAs(dst)) continue;
        if (found || !e.isPublic) return 0;
        found = e.address;
    }
    return const_cast<char *>(found);
}
