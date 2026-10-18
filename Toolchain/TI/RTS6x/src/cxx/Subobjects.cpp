// Spec: Itanium C++ ABI 2.9.5 (where a base lies: a non-virtual one at its offset, a virtual one at
// the offset the object's vtable holds at the listed place) and 2.9.7 (public paths).

#include "Subobjects.h"

namespace rts6x {

void Subobjects::add(const char *address, const void *type, bool isPublic)
{
    TypeInfo t(type);
    for (int i = 0; i < count_; i++) {
        if (entries_[i].address == address && TypeInfo(entries_[i].type).sameAs(t)) {
            entries_[i].isPublic = entries_[i].isPublic || isPublic;
            return;
        }
    }
    if (count_ == Capacity) {
        overflow_ = true;
        return;
    }
    Entry &e = entries_[count_++];
    e.type = type;
    e.address = address;
    e.isPublic = isPublic;
}

void Subobjects::walk(const char *address, const TypeInfo &type, bool isPublic)
{
    add(address, type.address(), isPublic);
    int n = type.baseCount();
    for (int i = 0; i < n; i++) {
        BaseClass b = type.base(i);
        const char *at = address + b.offset;
        if (b.isVirtual) {
            const char *vptr = *static_cast<const char *const *>(static_cast<const void *>(address));
            at = address + *static_cast<const int *>(static_cast<const void *>(vptr + b.offset));
        }
        walk(at, TypeInfo(b.type), isPublic && b.isPublic);
    }
}

void Subobjects::collect(const char *address, const TypeInfo &type)
{
    count_ = 0;
    overflow_ = false;
    walk(address, type, true);
}

bool Subobjects::hasPublic(const TypeInfo &target, const char *address) const
{
    for (int i = 0; i < count_; i++)
        if (entries_[i].isPublic && entries_[i].address == address && TypeInfo(entries_[i].type).sameAs(target))
            return true;
    return false;
}

}  // namespace rts6x
