// Spec: the Microsoft C++ ABI's RTTI on x64, as cpp11 1.29 emits it and vcruntime reads it: an object's
// vftable has its complete object locator (??_R4) in the slot before it - signature 1, the offset of
// this vfptr in the complete object, the construction displacement, then image-relative words: the
// type descriptor (??_R0: type_info's vftable, a spare word, the decorated name), the class hierarchy
// (??_R3: attributes, the base count, the base class array ??_R2) and the locator itself, which gives
// the image's base. Each base class descriptor (??_R1) names a type, how many bases lie under it in the
// array, where it lies (mdisp, and pdisp/vdisp through a vbtable for a virtual base) and its attributes.
//
//   __RTtypeid(p)            the type descriptor of *p's complete object
//   __RTCastToVoid(p)        the complete object
//   __RTDynamicCast(p, vfDelta, src, dst, isReference)   ISO C++11 5.2.7/8, as RTS6x's __dynamic_cast
//                            does it from the Itanium type_info (cxx\__dynamic_cast.cpp): the one dst
//                            subobject holding the src one (a down-cast), else, src being public in the
//                            whole object, dst if it is an unambiguous public base of it (a cross-cast)
//   __std_type_info_compare, __std_type_info_hash, __std_type_info_name    vcruntime's three over the
//                            decorated name, and name() undecorated as vcruntime's undecorator spells it
//                            ("struct Both", "int", "char const * __ptr64", "class std::vector<int>")
//
// A failed reference cast and typeid of a null pointer throw std::bad_cast and std::bad_typeid; until
// grt handles exceptions (see exceptions.cpp) they end the program with that said.

#include <stdlib.h>
#include <string.h>
#include "grt.h"

extern "C" void __grt_cxx_unhandled(const char *type, const char *what);

namespace {

struct TypeDescriptor {
    const void *vftable;
    char *undecorated;
    char name[1];
};
struct Displacement { int mdisp, pdisp, vdisp; };
struct BaseClass {
    int type;
    unsigned contained;
    Displacement where;
    unsigned attributes;
    int hierarchy;
};
struct Hierarchy { unsigned signature, attributes, count; int bases; };
struct Locator { unsigned signature, offset, cdOffset; int type, hierarchy, self; };

enum { NotVisible = 1, Ambiguous = 2 };

const Locator *locatorOf(const void *object)
{
    const void *const *vftable = *static_cast<const void *const *const *>(object);
    return static_cast<const Locator *>(vftable[-1]);
}

const char *imageOf(const Locator *l) { return reinterpret_cast<const char *>(l) - l->self; }

// The offsets are unsigned words, and taken from a pointer as 64-bit numbers: cpp11 1.29 negates
// `pointer - unsigned` in 32 bits before widening it on x86_64.
char *completeOf(void *object)
{
    const Locator *l = locatorOf(object);
    char *complete = static_cast<char *>(object) - static_cast<long long>(l->offset);
    if (l->cdOffset)
        complete -= *reinterpret_cast<const int *>(static_cast<const char *>(object) - static_cast<long long>(l->cdOffset));
    return complete;
}

bool same(const TypeDescriptor *a, const TypeDescriptor *b)
{
    return a == b || strcmp(a->name, b->name) == 0;
}

long offsetIn(const char *complete, const Displacement &d)
{
    long at = 0;
    if (d.pdisp >= 0) {
        const char *vbtable = *reinterpret_cast<const char *const *>(complete + d.pdisp);
        at = d.pdisp + *reinterpret_cast<const int *>(vbtable + d.vdisp);
    }
    return at + d.mdisp;
}

// ---- the undecorator: a decorated type name as vcruntime's type_info::name() spells it ----

class Text {
public:
    Text() : s_(0), n_(0), cap_(0) {}
    ~Text() { free(s_); }
    void add(char c) { grow(1); s_[n_++] = c; s_[n_] = 0; }
    void add(const char *t) { while (*t) add(*t++); }
    void add(const Text &t) { if (t.n_) add(t.s_); }
    void addPart(const char *t, size_t n) { for (size_t i = 0; i < n; i++) add(t[i]); }
    void insertFront(const char *t) {
        size_t m = strlen(t);
        grow(m);
        memmove(s_ + m, s_, n_ + 1);
        memcpy(s_, t, m);
        n_ += m;
    }
    void set(const Text &t) { n_ = 0; if (s_) s_[0] = 0; add(t); }
    const char *str() const { return s_ ? s_ : ""; }
    size_t size() const { return n_; }
    char last() const { return n_ ? s_[n_ - 1] : 0; }
    char *take() { char *r = s_; s_ = 0; n_ = cap_ = 0; return r; }
private:
    Text(const Text &);
    Text &operator=(const Text &);
    void grow(size_t more) {
        if (n_ + more + 1 <= cap_) return;
        size_t c = cap_ ? cap_ * 2 : 32;
        while (c < n_ + more + 1) c *= 2;
        char *p = static_cast<char *>(realloc(s_, c));
        if (!p) abort();
        s_ = p;
        cap_ = c;
    }
    char *s_;
    size_t n_, cap_;
};

class Undecorator {
public:
    explicit Undecorator(const char *p) : p_(p), bad_(false), names_(0), types_(0) {}
    ~Undecorator() { for (int i = 0; i < names_; i++) delete name_[i]; for (int i = 0; i < types_; i++) delete type_[i]; }

    // A type descriptor's name: ".?AV<class>@@", ".?AU<struct>@@", or a type's own code (".H", ".PEAD").
    bool run(Text &out) {
        if (*p_ == '.') p_++;
        if (p_[0] == '?' && p_[1] >= 'A' && p_[1] <= 'D') p_ += 2;
        type(out);
        return !bad_ && *p_ == 0;
    }

private:
    void type(Text &out);
    void qualifiedName(Text &out);
    void fragment(Text &out, bool remember);
    void templateArguments(Text &out);
    bool number(long long &v);
    void remember(Text **table, int &count, const Text &t) {
        if (count >= 10) return;
        Text *copy = new Text;
        copy->add(t);
        table[count++] = copy;
    }

    const char *p_;
    bool bad_;
    Text *name_[10];
    Text *type_[10];
    int names_, types_;
};

bool Undecorator::number(long long &v)
{
    bool negative = false;
    if (*p_ == '?') { negative = true; p_++; }
    if (*p_ >= '0' && *p_ <= '9') { v = *p_ - '0' + 1; p_++; }
    else {
        v = 0;
        while (*p_ >= 'A' && *p_ <= 'P') v = v * 16 + (*p_++ - 'A');
        if (*p_ != '@') { bad_ = true; return false; }
        p_++;
    }
    if (negative) v = -v;
    return true;
}

void Undecorator::templateArguments(Text &out)
{
    // a template's arguments have back-references of their own
    Text *savedNames[10], *savedTypes[10];
    int nNames = names_, nTypes = types_;
    for (int i = 0; i < 10; i++) { savedNames[i] = name_[i]; savedTypes[i] = type_[i]; }
    names_ = types_ = 0;
    out.add('<');
    bool first = true;
    while (!bad_ && *p_ && *p_ != '@') {
        if (!first) out.add(',');
        first = false;
        Text arg;
        if (p_[0] == '$' && p_[1] == '0') {
            p_ += 2;
            long long v;
            if (!number(v)) break;
            char digits[24];
            int n = 0;
            unsigned long long u = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
            do { digits[n++] = (char)('0' + u % 10); u /= 10; } while (u);
            if (v < 0) arg.add('-');
            while (n) arg.add(digits[--n]);
        } else {
            // a type spelt in more than one character is remembered for the back-references 0-9
            const char *before = p_;
            type(arg);
            if (p_ - before > 1) remember(type_, types_, arg);
        }
        out.add(arg);
    }
    if (*p_ == '@') p_++;
    if (out.last() == '>') out.add(' ');
    out.add('>');
    for (int i = 0; i < names_; i++) delete name_[i];
    for (int i = 0; i < types_; i++) delete type_[i];
    names_ = nNames;
    types_ = nTypes;
    for (int i = 0; i < 10; i++) { name_[i] = savedNames[i]; type_[i] = savedTypes[i]; }
}

void Undecorator::fragment(Text &out, bool keep)
{
    if (*p_ >= '0' && *p_ <= '9') {
        int k = *p_++ - '0';
        if (k < names_) out.add(*name_[k]); else bad_ = true;
        return;
    }
    if (p_[0] == '?' && p_[1] == '$') {
        p_ += 2;
        Text t;
        fragment(t, false);
        templateArguments(t);
        out.add(t);
        if (keep) remember(name_, names_, t);
        return;
    }
    const char *start = p_;
    while (*p_ && *p_ != '@') p_++;
    if (*p_ != '@' || p_ == start) { bad_ = true; return; }
    Text t;
    t.addPart(start, (size_t)(p_ - start));
    p_++;
    out.add(t);
    if (keep) remember(name_, names_, t);
}

void Undecorator::qualifiedName(Text &out)
{
    // innermost first: Inner@Outer@ns@@ is ns::Outer::Inner
    Text parts[16];
    int n = 0;
    while (!bad_ && *p_ && *p_ != '@') {
        if (n == 16) { bad_ = true; return; }
        fragment(parts[n++], true);
    }
    if (*p_ != '@') { bad_ = true; return; }
    p_++;
    for (int i = n - 1; i >= 0; i--) {
        out.add(parts[i]);
        if (i > 0) out.add("::");
    }
}

void Undecorator::type(Text &out)
{
    char c = *p_++;
    switch (c) {
    case 'C': out.add("signed char"); return;
    case 'D': out.add("char"); return;
    case 'E': out.add("unsigned char"); return;
    case 'F': out.add("short"); return;
    case 'G': out.add("unsigned short"); return;
    case 'H': out.add("int"); return;
    case 'I': out.add("unsigned int"); return;
    case 'J': out.add("long"); return;
    case 'K': out.add("unsigned long"); return;
    case 'M': out.add("float"); return;
    case 'N': out.add("double"); return;
    case 'O': out.add("long double"); return;
    case 'X': out.add("void"); return;
    case '_':
        c = *p_++;
        switch (c) {
        case 'J': out.add("__int64"); return;
        case 'K': out.add("unsigned __int64"); return;
        case 'N': out.add("bool"); return;
        case 'W': out.add("wchar_t"); return;
        case 'S': out.add("char16_t"); return;
        case 'U': out.add("char32_t"); return;
        }
        bad_ = true;
        return;
    case 'T': out.add("union "); qualifiedName(out); return;
    case 'U': out.add("struct "); qualifiedName(out); return;
    case 'V': out.add("class "); qualifiedName(out); return;
    case 'W':
        if (*p_ >= '0' && *p_ <= '7') p_++;
        out.add("enum ");
        qualifiedName(out);
        return;
    case 'P': case 'Q': case 'R': case 'S': case 'A': {
        bool reference = c == 'A';
        if (p_[0] == '$' && p_[1] == '$' && p_[2] == 'Q') { bad_ = true; return; }
        bool ptr64 = false;
        while (*p_ == 'E' || *p_ == 'F' || *p_ == 'I') { if (*p_ == 'E') ptr64 = true; p_++; }
        char cv = *p_++;
        if (cv < 'A' || cv > 'D') { bad_ = true; return; }
        if (*p_ == '6') { bad_ = true; return; }     // a pointer to a function: not spelt here
        Text pointee;
        type(pointee);
        out.add(pointee);
        if (cv == 'B' || cv == 'D') out.add(" const");
        if (cv == 'C' || cv == 'D') out.add(" volatile");
        out.add(reference ? " &" : " *");
        if (ptr64) out.add(" __ptr64");
        if (c == 'Q' || c == 'S') out.add(" const");
        if (c == 'R' || c == 'S') out.add(" volatile");
        return;
    }
    case '$':
        if (p_[0] == '$' && p_[1] == 'T') { p_ += 2; out.add("std::nullptr_t"); return; }
        if (p_[0] == '$' && p_[1] == 'Q') {
            p_ += 2;
            bool ptr64 = false;
            while (*p_ == 'E' || *p_ == 'F' || *p_ == 'I') { if (*p_ == 'E') ptr64 = true; p_++; }
            char cv = *p_++;
            if (cv < 'A' || cv > 'D') { bad_ = true; return; }
            Text pointee;
            type(pointee);
            out.add(pointee);
            if (cv == 'B' || cv == 'D') out.add(" const");
            if (cv == 'C' || cv == 'D') out.add(" volatile");
            out.add(" &&");
            if (ptr64) out.add(" __ptr64");
            return;
        }
        bad_ = true;
        return;
    default:
        if (c >= '0' && c <= '9') {
            int k = c - '0';
            if (k < types_) out.add(*type_[k]); else bad_ = true;
            return;
        }
        bad_ = true;
        return;
    }
}

}  // namespace

// vcruntime's: an undecorated name, or the decorated one past its '.' where it cannot be read
extern "C" char *__grt_undecorate(const char *decorated)
{
    Text out;
    Undecorator u(decorated);
    if (!u.run(out)) {
        Text raw;
        raw.add(decorated[0] == '.' ? decorated + 1 : decorated);
        return raw.take();
    }
    return out.take();
}

struct __type_info_node {
    void *next;
};

__type_info_node __type_info_root_node;

struct __std_type_info_data {
    const char *undecorated;
    const char decorated[1];
};

extern "C" int __std_type_info_compare(const __std_type_info_data *a, const __std_type_info_data *b)
{
    if (a == b) return 0;
    return strcmp(a->decorated + 1, b->decorated + 1);
}

extern "C" unsigned long long __std_type_info_hash(const __std_type_info_data *data)
{
    unsigned long long h = 14695981039346656037ull;
    for (const char *p = data->decorated + 1; *p; p++) {
        h ^= (unsigned char)*p;
        h *= 1099511628211ull;
    }
    return h;
}

extern "C" const char *__std_type_info_name(__std_type_info_data *data, __type_info_node *root)
{
    (void)root;
    if (!data->undecorated) data->undecorated = __grt_undecorate(data->decorated);
    return data->undecorated;
}

extern "C" void *__RTCastToVoid(void *object)
{
    return object ? completeOf(object) : 0;
}

extern "C" void *__RTtypeid(void *object)
{
    if (!object) __grt_cxx_unhandled("class std::bad_typeid", "Attempted a typeid of nullptr pointer!");
    const Locator *l = locatorOf(object);
    return const_cast<char *>(imageOf(l) + l->type);
}

extern "C" void *__RTDynamicCast(void *object, long vfDelta, void *source, void *target, int isReference)
{
    if (!object) return 0;
    const Locator *l = locatorOf(object);
    const char *image = imageOf(l);
    char *complete = completeOf(object);
    const char *at = static_cast<char *>(object) - vfDelta;
    const Hierarchy *h = reinterpret_cast<const Hierarchy *>(image + l->hierarchy);
    const int *bases = reinterpret_cast<const int *>(image + h->bases);
    const TypeDescriptor *src = static_cast<const TypeDescriptor *>(source);
    const TypeDescriptor *dst = static_cast<const TypeDescriptor *>(target);
    unsigned n = h->count;
    long srcAt = (long)(at - complete);

    // the source subobject in the flattened list of the complete object's bases
    int s = -1;
    for (unsigned i = 0; i < n && s < 0; i++) {
        const BaseClass *b = reinterpret_cast<const BaseClass *>(image + bases[i]);
        if (same(reinterpret_cast<const TypeDescriptor *>(image + b->type), src) && offsetIn(complete, b->where) == srcAt)
            s = (int)i;
    }
    // a down-cast: the dst subobjects holding the source one (it lies among the bases under them)
    char *found = 0;
    int downs = 0;
    for (unsigned i = 0; i < n && s >= 0; i++) {
        const BaseClass *b = reinterpret_cast<const BaseClass *>(image + bases[i]);
        if (!same(reinterpret_cast<const TypeDescriptor *>(image + b->type), dst)) continue;
        if ((unsigned)s < i || (unsigned)s > i + b->contained) continue;
        char *address = complete + offsetIn(complete, b->where);
        if (address != found) { found = address; downs++; }
    }
    if (downs == 1) return found;
    // a cross-cast: src public in the whole, dst an unambiguous public base of it
    bool ok = false;
    found = 0;
    if (downs == 0 && s >= 0 && !(reinterpret_cast<const BaseClass *>(image + bases[s])->attributes & NotVisible)) {
        ok = true;
        for (unsigned i = 0; i < n && ok; i++) {
            const BaseClass *b = reinterpret_cast<const BaseClass *>(image + bases[i]);
            if (!same(reinterpret_cast<const TypeDescriptor *>(image + b->type), dst)) continue;
            char *address = complete + offsetIn(complete, b->where);
            if ((b->attributes & (NotVisible | Ambiguous)) || (found && found != address)) ok = false;
            found = address;
        }
    }
    if (ok && found) return found;
    if (isReference) __grt_cxx_unhandled("class std::bad_cast", "Bad dynamic_cast!");
    return 0;
}
