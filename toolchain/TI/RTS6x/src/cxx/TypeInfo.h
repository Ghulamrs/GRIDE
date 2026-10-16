// Spec: Itanium C++ ABI 2.9.5 - the layouts of the type_info objects a compiler emits: a vtable
// address and the name, then per class: nothing, one public non-virtual base at offset 0
// (__si_class_type_info), or flags and a list of bases with their offsets (__vmi_class_type_info).
#ifndef RTS6X_TYPE_INFO_H
#define RTS6X_TYPE_INFO_H

extern "C" {
extern const char __rts6x_vt_fundamental[], __rts6x_vt_class[], __rts6x_vt_si_class[];
extern const char __rts6x_vt_vmi_class[], __rts6x_vt_pointer[], __rts6x_vt_pointer_to_member[];
}

namespace rts6x {

// One base of a class as its type_info lists it.
struct BaseClass {
    const void *type;
    bool isVirtual;
    bool isPublic;
    // A non-virtual base's offset in the object; a virtual one's, the place in the vtable that holds it.
    int offset;
};

class TypeInfo {
public:
    enum Kind { Other, Fundamental, Class, SingleBase, MultipleBases, Pointer, MemberPointer };

    explicit TypeInfo(const void *object) : words_(static_cast<const char *const *>(object)) {}

    const void *address() const { return words_; }
    Kind kind() const;
    const char *name() const { return words_[1]; }
    // The same type: one object, or two with the same name (weak copies the linker kept apart).
    bool sameAs(const TypeInfo &other) const;
    bool isClass() const { Kind k = kind(); return k == Class || k == SingleBase || k == MultipleBases; }

    // A class's direct bases, as its type_info lists them.
    int baseCount() const;
    BaseClass base(int i) const;

    // __pbase_type_info: the qualifier flags and the type pointed to.
    unsigned pointerFlags() const { return static_cast<const unsigned *>(static_cast<const void *>(words_))[2]; }
    const void *pointee() const { return words_[3]; }

private:
    const char *const *words_;
};

}  // namespace rts6x

#endif
