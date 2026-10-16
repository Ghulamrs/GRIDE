// Spec: Itanium C++ ABI 2.9.5 - __vmi_class_type_info's base list: each base a type_info pointer
// and a word whose low byte holds __virtual_mask (1) and __public_mask (2) and whose upper 24 bits,
// signed, are the offset.

#include <string.h>
#include "TypeInfo.h"

namespace rts6x {

TypeInfo::Kind TypeInfo::kind() const
{
    const char *vptr = words_[0];
    if (vptr == __rts6x_vt_class) return Class;
    if (vptr == __rts6x_vt_si_class) return SingleBase;
    if (vptr == __rts6x_vt_vmi_class) return MultipleBases;
    if (vptr == __rts6x_vt_pointer) return Pointer;
    if (vptr == __rts6x_vt_pointer_to_member) return MemberPointer;
    if (vptr == __rts6x_vt_fundamental) return Fundamental;
    return Other;
}

bool TypeInfo::sameAs(const TypeInfo &other) const
{
    return words_ == other.words_ || strcmp(name(), other.name()) == 0;
}

int TypeInfo::baseCount() const
{
    switch (kind()) {
    case SingleBase: return 1;
    case MultipleBases: return (int)static_cast<const unsigned *>(static_cast<const void *>(words_))[3];
    default: return 0;
    }
}

BaseClass TypeInfo::base(int i) const
{
    BaseClass b;
    if (kind() == SingleBase) {
        b.type = words_[2];
        b.isVirtual = false;
        b.isPublic = true;
        b.offset = 0;
        return b;
    }
    const char *const *entry = words_ + 4 + 2 * i;
    int flags = static_cast<const int *>(static_cast<const void *>(entry))[1];
    b.type = entry[0];
    b.isVirtual = (flags & 1) != 0;
    b.isPublic = (flags & 2) != 0;
    b.offset = flags >> 8;
    return b;
}

}  // namespace rts6x
