// Spec: ISO C++11 15.3/3 - a handler of type T takes an exception object of type E when they are
// the same type, T is an unambiguous public base of E, or both are pointers and E converts to T by
// a qualification or a pointer conversion to such a base or to void; nullptr_t to any pointer.

#include "Exception.h"
#include "../cxx/Subobjects.h"
#include "../cxx/TypeInfo.h"

extern "C" char __TI_STATIC_BASE[];

namespace rts6x {

namespace {

Subobjects found;

// The one public subobject of type target in the object at address of type type, or null.
const char *publicBase(const char *address, const TypeInfo &type, const TypeInfo &target)
{
    found.collect(address, type);
    if (found.overflow()) return 0;
    const char *at = 0;
    for (int i = 0; i < found.count(); i++) {
        const Subobjects::Entry &e = found.at(i);
        if (!TypeInfo(e.type).sameAs(target)) continue;
        if (at || !e.isPublic) return 0;
        at = e.address;
    }
    return at;
}

// Whether target is a public base of type, asked of the types alone.
bool isPublicBase(const TypeInfo &type, const TypeInfo &target)
{
    if (type.sameAs(target)) return true;
    for (int i = 0; i < type.baseCount(); i++) {
        BaseClass b = type.base(i);
        if (b.isPublic && isPublicBase(TypeInfo(b.type), target)) return true;
    }
    return false;
}

}  // namespace

bool Exception::matches(unsigned catchType)
{
    // A descriptor's type is the type_info's offset from the static base (R_C6000_EHTYPE).
    TypeInfo thrown(type), handler(__TI_STATIC_BASE + catchType);
    char *obj = static_cast<char *>(object());
    TypeInfo::Kind tk = thrown.kind(), hk = handler.kind();
    if (hk == TypeInfo::Pointer) {
        void *value = *reinterpret_cast<void **>(obj);
        if (tk == TypeInfo::Fundamental && thrown.name()[0] == 'D' && thrown.name()[1] == 'n') value = 0;
        else if (tk != TypeInfo::Pointer || (thrown.pointerFlags() & ~handler.pointerFlags()) != 0) return false;
        else {
            TypeInfo tp(thrown.pointee()), hp(handler.pointee());
            if (tp.sameAs(hp) || (hp.kind() == TypeInfo::Fundamental && hp.name()[0] == 'v' && hp.name()[1] == 0)) {
            } else if (tp.isClass() && hp.isClass()) {
                if (value) {
                    value = const_cast<char *>(publicBase(static_cast<char *>(value), tp, hp));
                    if (!value) return false;
                } else if (!isPublicBase(tp, hp)) {
                    return false;
                }
            } else {
                return false;
            }
        }
        pointer = value;
        adjusted = &pointer;
        return true;
    }
    if (thrown.sameAs(handler)) {
        adjusted = obj;
        return true;
    }
    if (!thrown.isClass() || !handler.isClass()) return false;
    const char *at = publicBase(obj, thrown, handler);
    if (!at) return false;
    adjusted = const_cast<char *>(at);
    return true;
}

}  // namespace rts6x
