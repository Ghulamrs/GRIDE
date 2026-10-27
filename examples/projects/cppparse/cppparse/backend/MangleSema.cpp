// MangleSema.cpp - MSVC x64 name decoration from the version 6 symbols and types (same encoding as Mangle.cpp).
#include "Mangle.hpp"
#include "PathUtil.hpp"
#include "Sema.hpp"

#include <sstream>
#include <vector>

namespace cppparse {

namespace {

class SemaMangler {
public:
    // vtordispex: cl's vtordispex thunk's vbptr distance, vbtable entry offset and adjustor ($R4), with vtordisp
    // adjustorToReturn: the adjustor thunk to a return thunk (cl: W, the result override, ?g@D@@WBA@EAAPEAUX@@XZ)
    std::string function(const sema::Symbol& f, long thunk = 0, const sema::Type* resultOverride = 0, bool vtordisp = false, const long* vtordispex = 0, bool adjustorToReturn = false);
    std::string variable(const sema::Symbol& v);
    std::string initializerEntry(const sema::Symbol& v);
    std::string staticLocal(const sema::Symbol& v, const sema::Symbol& fn, long discriminator);
    std::string staticGuard(const sema::Symbol& fn, long discriminator, int index);
    std::string vtable(const sema::Symbol& c, const std::vector<const sema::Symbol*>& path);
    std::string vbtable(const sema::Symbol& c, const std::vector<sema::Symbol*>& path);
    std::string displacementMap(const sema::Symbol& from, const sema::Symbol& to);
    std::string vcallThunk(const sema::Symbol& c, long slotOffset);
    // RTTI: the name a type descriptor holds (".?AUD@@", ".H") and the descriptors' symbols
    std::string rttiTypeName(const sema::Type* t);
    std::string typeDescriptor(const sema::Type* t) { return "??_R0" + rttiTypeName(t).substr(1) + "@8"; }
    std::string classHierarchy(const sema::Symbol& c) { return "??_R3" + classScoped(c) + "8"; }
    std::string baseClassArray(const sema::Symbol& c) { return "??_R2" + classScoped(c) + "8"; }
    std::string baseClassDescriptor(const sema::Symbol& c, long mdisp, long pdisp, long vdisp, long attributes);
    std::string completeObjectLocator(const sema::Symbol& c, const std::vector<const sema::Symbol*>& path) { std::string v = vtable(c, path); return "??_R4" + v.substr(4); }
private:
    std::string classScoped(const sema::Symbol& c) { return symbolName(&c) + scopesOf(c.scope, true) + "@"; }
    std::string storageType(const sema::Type* t);
    std::string memberPointerType(const sema::Type* t);
    std::string memberClass(const sema::Type* t);
    std::string number(long long v);
    std::string name(const std::string& component);
    std::string symbolName(const sema::Symbol* s);
    std::string templateName(const sema::Symbol* inst, const std::string& base, bool baseIsOperatorCode, bool isFunction);
    std::string scopesOf(const sema::Scope* s, bool ofClass = false);   // ofClass: the scopes of a class symbol itself
    std::string type(const sema::Type* t);
    std::string resultType(const sema::Type* t);
    std::string templateArgType(const sema::Type* t);
    std::string paramType(const sema::Type* t);
    std::string paramList(const sema::Type* fn);
    std::string classType(const sema::Type* t);
    std::string operatorCode(const std::string& fname, bool& ok);
    std::vector<std::string> m_names;
    std::vector<std::string> m_argTypes;   // keyed by the encoding without back-references: one entry per distinct type
};

std::string cvCode(const sema::Type* t)
{
    if (!t) return "A";
    return t->isConst ? (t->isVolatile ? "D" : "B") : (t->isVolatile ? "C" : "A");
}

bool isPointerLike(const sema::Type* t)
{
    return t && (t->kind == sema::Type::Pointer || t->kind == sema::Type::Reference || t->kind == sema::Type::MemberPointer || t->kind == sema::Type::NullPtr);
}

std::string SemaMangler::number(long long v)
{
    std::string out;
    unsigned long long u = static_cast<unsigned long long>(v);
    if (v < 0) { out += '?'; u = 0ULL - u; }   // the magnitude (of the most negative value too)
    if (u == 0) return out + "A@";
    if (u <= 10) { out += static_cast<char>('0' + static_cast<int>(u - 1)); return out; }
    std::string hex;
    while (u) { hex.insert(hex.begin(), static_cast<char>('A' + static_cast<int>(u & 15))); u >>= 4; }
    return out + hex + "@";
}

std::string SemaMangler::name(const std::string& component)
{
    for (size_t i = 0; i < m_names.size(); ++i)
        if (m_names[i] == component) return std::string(1, static_cast<char>('0' + i));
    if (m_names.size() < 10) m_names.push_back(component);
    return component + "@";
}

// "?$Buffer@H$03@": the template name and its arguments start fresh back-reference tables (the template's
// own name is their first entry); a class template instance then takes one slot of the enclosing table,
// a function template instance takes none
std::string SemaMangler::templateName(const sema::Symbol* inst, const std::string& base, bool baseIsOperatorCode, bool isFunction)
{
    std::vector<std::string> savedNames = m_names, savedArgs = m_argTypes;
    m_names.clear(); m_argTypes.clear();
    std::string enc = "?$";
    enc += baseIsOperatorCode ? base : name(base);   // "?$?8HH@" for operator==<int, int>
    for (size_t i = 0; i < inst->templateArgs.size(); ++i) {
        const sema::Symbol* member = i < inst->templateSymArgs.size() ? inst->templateSymArgs[i] : 0;
        if (inst->templateArgs[i]) enc += templateArgType(inst->templateArgs[i]);
        else if (member && (member->kind == sema::Symbol::Function || member->ownerClass->memberPointerUnspecified || !member->ownerClass->vbases.empty())) {
            // a pointer to member function: $1 and the function's decorated name (a virtual one's vcall thunk), in a
            // class of multiple inheritance $H, the name and the this adjustment; of virtual inheritance $I, the name,
            // the adjustment and the vbtable offset; in the unspecified model $J, the name, the adjustment, the vbptr's
            // offset and the vbtable offset. A data member with virtual bases $F, its offset and the vbtable offset,
            // unspecified $G, the offset, the vbptr's offset and the vbtable offset (else its offset, $0, below); the
            // numbers of those forms are spelled as unsigned 32-bit values ({fx, -16, 0}: $I?fx@X@@QEBAHXZPPPPPPPA@A@)
            bool multiple = false, virt = false;
            sema::TypeTable::memberPointerModel(member->ownerClass, multiple, virt, 0);
            const bool unspecified = !member->ownerClass->isDefinition || member->ownerClass->memberPointerUnspecified;
            int off = 0, adj = 0, vbptr = 0, vbindex = 0;
            sema::TypeTable::memberPointerValue(member, off, adj, vbptr, vbindex);
            const std::string nOff = number(static_cast<long long>(static_cast<unsigned int>(off)));
            const std::string nAdj = number(static_cast<long long>(static_cast<unsigned int>(adj)));
            const std::string nVbptr = number(static_cast<long long>(static_cast<unsigned int>(vbptr)));
            const std::string nIndex = number(static_cast<long long>(static_cast<unsigned int>(vbindex)));
            if (member->kind == sema::Symbol::Function) {
                SemaMangler nested;
                const std::string fn = member->isVirtual ? nested.vcallThunk(*member->ownerClass, 8L * (member->vtableSlot < 0 ? 0 : member->vtableSlot)) : nested.function(*member);
                if (unspecified) enc += "$J" + fn + nAdj + nVbptr + nIndex;
                else if (virt) enc += "$I" + fn + nAdj + nIndex;
                else enc += multiple ? "$H" + fn + number(member->thisAdjust) : "$1" + fn;
            }
            else enc += unspecified ? "$G" + nOff + nVbptr + nIndex : "$F" + nOff + nIndex;
        }
        else {
            // the value as a signed 64-bit number: an unsigned parameter of 4 bytes (held sign-extended, see
            // sema::foldedConstant) widens with zeros (X<0xFFFFFFFFu> is $0PPPPPPPP@), a 64-bit one keeps its bits
            long long v = i < inst->templateIntArgs.size() ? inst->templateIntArgs[i] : 0;
            const sema::Symbol* from = inst->instantiatedFrom;
            const sema::Type* pt = from && i < from->templateParams.size() && from->templateParams[i] ? from->templateParams[i]->type : 0;
            if (pt && (pt->kind == sema::Type::UInt || pt->kind == sema::Type::ULong)) v = static_cast<long long>(static_cast<unsigned long long>(v) & 0xFFFFFFFFULL);
            enc += "$0" + number(v);
        }
    }
    enc += "@";
    m_names = savedNames; m_argTypes = savedArgs;
    if (isFunction) return enc;
    for (size_t i = 0; i < m_names.size(); ++i)
        if (m_names[i] == enc) return std::string(1, static_cast<char>('0' + i));
    if (m_names.size() < 10) m_names.push_back(enc);
    return enc;
}

std::string SemaMangler::symbolName(const sema::Symbol* s)
{
    if (s->kind == sema::Symbol::Class && s->instantiatedFrom) return templateName(s, s->instantiatedFrom->name, false, false);
    if (s->kind == sema::Symbol::Namespace && s->name.empty()) {
        std::string hex;
        for (int i = 7; i >= 0; --i) hex += "0123456789abcdef"[(g_mangleUnitId >> (4 * i)) & 15];
        return name("?A0x" + hex);
    }
    return name(s->name);
}

std::string SemaMangler::scopesOf(const sema::Scope* s, bool ofClass)
{
    std::string out;
    bool local = ofClass;   // the scopes of a class itself: declared in a function, it is a local class
    for (; s; s = s->parent) {
        if ((s->kind == sema::Scope::Namespace || s->kind == sema::Scope::Class) && s->owner) { out += symbolName(s->owner); if (s->kind == sema::Scope::Class) local = true; }
        if (s->kind == sema::Scope::Function || s->kind == sema::Scope::Block) {
            // a local class (the scopes of a class were passed): cl's local scope, "?<block>?<the function's name>", whose
            // closing "@" the caller supplies ("?f@L@?1??t4@@YAHH@Z@SAHH@Z"); the body is block 2, a nested block gets
            // 2 plus its number (unique in the function, cl's own numbering only for the body)
            if (local) {
                const sema::Scope* f = s;
                while (f && f->kind != sema::Scope::Function) f = f->parent;
                if (f && f->owner) {
                    std::string fn = function(*f->owner);
                    if (fn.empty() || fn[0] != '?') fn = "?" + fn + "@@9";
                    out += "?" + number(2 + (s->kind == sema::Scope::Block ? s->blockIndex : 0)) + "?" + fn;
                }
            }
            break;
        }
    }
    return out;
}

std::string SemaMangler::classType(const sema::Type* t)
{
    const sema::Symbol* c = t->decl;
    char letter = c && c->isUnion ? 'T' : c && c->isStruct ? 'U' : 'V';
    if (!c) return std::string(1, letter) + "unknown@@";
    std::string out(1, letter);
    out += symbolName(c);          // before the scopes: the back-reference table follows the call order
    out += scopesOf(c->scope, true);
    return out + "@";
}

std::string SemaMangler::type(const sema::Type* t)
{
    if (!t) return "X";
    switch (t->kind) {
    case sema::Type::Void: return "X";
    case sema::Type::Bool: return "_N";
    case sema::Type::Char: return "D";
    case sema::Type::SChar: return "C";
    case sema::Type::UChar: return "E";
    case sema::Type::WChar: return "_W";
    case sema::Type::Short: return "F";
    case sema::Type::UShort: return "G";
    case sema::Type::Int: return "H";
    case sema::Type::UInt: return "I";
    case sema::Type::Long: return "J";
    case sema::Type::ULong: return "K";
    case sema::Type::LongLong: return "_J";
    case sema::Type::ULongLong: return "_K";
    case sema::Type::Float: return "M";
    case sema::Type::Double: return "N";
    case sema::Type::LongDouble: return "O";
    case sema::Type::NullPtr: return "PEAX";
    case sema::Type::Pointer: case sema::Type::Array: {
        const sema::Type* in = t->inner;
        const char* ptr = t->isConst ? (t->isVolatile ? "S" : "Q") : (t->isVolatile ? "R" : "P");
        if (t->kind == sema::Type::Array) ptr = "Q";   // an array parameter is a const pointer
        if (in && in->kind == sema::Type::Function) {
            std::string s = std::string(ptr) + "6A";
            s += resultType(in->inner);
            return s + paramList(in) + "Z";
        }
        return std::string(ptr) + "E" + cvCode(in) + type(in);
    }
    case sema::Type::Reference: return "AE" + cvCode(t->inner) + type(t->inner);
    case sema::Type::Class: return classType(t);
    case sema::Type::Enum: {
        if (!t->decl) return "W4enum@@";
        std::string s = "W4";
        s += symbolName(t->decl);
        s += scopesOf(t->decl->scope);
        return s + "@";
    }
    case sema::Type::Function: {
        std::string s = "$$A6A";
        s += resultType(t->inner);
        return s + paramList(t) + "Z";
    }
    case sema::Type::MemberPointer: return memberPointerType(t);
    default: return "X";
    }
}

// the class of a pointer to member as its pointee's qualifier: the name and scopes without the U/V letter
std::string SemaMangler::memberClass(const sema::Type* t)
{
    const sema::Type* c = t->memberClass;
    if (!c || c->kind != sema::Type::Class || !c->decl) return "unknown@@";
    return classType(c).substr(1);
}

// a pointer to data member: the pointer (P, Q const, R volatile, S both), __ptr64, the member's class as the
// pointee's qualifier (Q, R for a const member, S volatile, T both) and the member's type: "PEQP@@H"; to a member
// function: "P8P@@EAAHXZ", the class, then the function's this qualifier (A, B const), calling convention, result
// and parameters
std::string SemaMangler::memberPointerType(const sema::Type* t)
{
    const char* ptr = t->isConst ? (t->isVolatile ? "S" : "Q") : (t->isVolatile ? "R" : "P");
    const sema::Type* in = t->inner;
    if (in && in->kind == sema::Type::Function) {
        std::string s = std::string(ptr) + "8" + memberClass(t) + "E" + (in->constMethod ? "B" : "A") + "A";
        s += resultType(in->inner);
        return s + paramList(in) + "Z";
    }
    const char* mq = in && in->isConst ? (in->isVolatile ? "T" : "R") : (in && in->isVolatile ? "S" : "Q");
    return std::string(ptr) + "E" + mq + memberClass(t) + type(in);
}

// a class, union or enum result, and a cv-qualified non-pointer one, is prefixed with "?" and its qualifiers
std::string SemaMangler::resultType(const sema::Type* t)
{
    if (t && (t->kind == sema::Type::Class || t->kind == sema::Type::Enum || (!isPointerLike(t) && (t->isConst || t->isVolatile))))
        return "?" + cvCode(t) + type(t);
    return type(t);
}

// a cv-qualified non-pointer template argument is escaped: pair<const string, int> -> "$$CBVstring@std@@H"
std::string SemaMangler::templateArgType(const sema::Type* t)
{
    if (t && !isPointerLike(t) && (t->isConst || t->isVolatile)) return "$$C" + cvCode(t) + type(t);
    return type(t);
}

// parameters encoded in more than one character get a slot of the argument table; the key is the
// encoding without back-references, so two spellings of one type ("AEBVstring@std@@", "AEBV12@") match,
// plus the top-level qualifiers, which the encoding drops but which still make "const string" a type of its own
std::string SemaMangler::paramType(const sema::Type* t)
{
    SemaMangler plain;
    std::string key = cvCode(t) + plain.type(t);
    for (size_t i = 0; i < m_argTypes.size(); ++i)
        if (m_argTypes[i] == key) return std::string(1, static_cast<char>('0' + i));
    std::string enc = type(t);
    if (enc.size() > 1 && m_argTypes.size() < 10) m_argTypes.push_back(key);
    return enc;
}

std::string SemaMangler::paramList(const sema::Type* fn)
{
    if (fn->params.empty()) return fn->variadic ? "Z" : "X";
    std::string s;
    for (size_t i = 0; i < fn->params.size(); ++i) s += paramType(fn->params[i]);
    return s + (fn->variadic ? "Z" : "@");
}

std::string SemaMangler::operatorCode(const std::string& fname, bool& ok)
{
    static const char* const table[][2] = {
        {"=", "4"}, {"+", "H"}, {"-", "G"}, {"*", "D"}, {"/", "K"}, {"%", "L"}, {"==", "8"}, {"!=", "9"},
        {"<", "M"}, {">", "O"}, {"<=", "N"}, {">=", "P"}, {"[]", "A"}, {"()", "R"}, {"->", "C"}, {"++", "E"},
        {"--", "F"}, {"!", "7"}, {"|", "U"}, {"&", "I"}, {"^", "T"}, {"~", "S"}, {">>", "5"}, {"<<", "6"},
        {"+=", "Y"}, {"-=", "Z"}, {"*=", "X"}, {"/=", "_0"}, {"%=", "_1"}, {">>=", "_2"}, {"<<=", "_3"},
        {"&=", "_4"}, {"|=", "_5"}, {"^=", "_6"}, {"&&", "V"}, {"||", "W"}, {",", "Q"}, {"->*", "J"},
        {"new", "2"}, {"delete", "3"}, {"new[]", "_U"}, {"delete[]", "_V"}
    };
    std::string op = trim(fname.substr(8));
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
        if (op == table[i][0]) { ok = true; return table[i][1]; }
    ok = false;
    return "";
}

// resultOverride: a return-adjusting thunk is spelled as the function, non-virtual, returning the overridden
// function's type; vtordisp: cl's $4 thunk, the vtordisp's place (-4, as an unsigned hex number) and the adjustment
// in place of the access code
std::string SemaMangler::function(const sema::Symbol& f, long thunk, const sema::Type* resultOverride, bool vtordisp, const long* vtordispex, bool adjustorToReturn)
{
    const sema::Symbol* owner = f.ownerClass;
    if (f.isExternC) return f.name;
    if (f.name == "main" && !owner && f.scope && f.scope->kind == sema::Scope::Global) return "main";
    // an instance of a member template keeps the template's access and, for a constructor template, its "?0"
    const sema::Symbol* tmpl = f.instantiatedFrom && !f.templateArgs.empty() ? f.instantiatedFrom : 0;
    bool ctor = f.isConstructor || (tmpl && owner && tmpl->name == owner->ctorName());
    cxx::Access access = tmpl && tmpl->ownerClass ? tmpl->access : f.access;
    std::string out = "?";
    if (tmpl) {
        bool ok = ctor;
        std::string base = ctor ? std::string("?0") : tmpl->name;
        if (!ctor && base.compare(0, 8, "operator") == 0) {
            std::string code = operatorCode(base, ok);
            if (ok) base = "?" + code;
        }
        out += templateName(&f, base, ok, true);   // "??$?0PEAH@" for a template constructor
    }
    else if (ctor) out += "?0";
    else if (f.isDestructor) out += "?1";
    else if (f.isConversion) out += "?B";
    else if (f.isOperator) { bool ok; std::string code = operatorCode(f.name, ok); out += ok ? "?" + code : name(f.name); }
    else out += name(f.name);
    out += scopesOf(f.scope) + "@";
    const sema::Type* ft = f.type;
    if (owner && !f.isStatic) {
        char acc = access == cxx::AccessPrivate ? 'A' : access == cxx::AccessProtected ? 'I' : 'Q';
        if (f.isVirtual && (!resultOverride || adjustorToReturn)) acc = static_cast<char>(acc + 4);
        if (thunk) acc = static_cast<char>(acc + 2);   // an adjustor thunk (G, O, W) and its adjustment
        // cl spells a vtordisp thunk's numbers as unsigned 32-bit values ($4PPPPPPPM@PPPPPPMI@: -4, -56)
        if (vtordispex) {
            out += "$R4";
            out += number(static_cast<long long>(static_cast<unsigned int>(vtordispex[0])));
            out += number(static_cast<long long>(static_cast<unsigned int>(vtordispex[1])));
            out += "PPPPPPPM@";
            out += number(static_cast<long long>(static_cast<unsigned int>(vtordispex[2])));
        }
        else if (vtordisp) { out += "$4PPPPPPPM@"; out += number(static_cast<long long>(static_cast<unsigned int>(thunk))); }
        else { out += acc; if (thunk) out += number(thunk); }
        out += "E";
        out += ft && ft->constMethod ? "B" : "A";
        out += "A";
    } else if (owner) {
        out += access == cxx::AccessPrivate ? "C" : access == cxx::AccessProtected ? "K" : "S";
        out += "A";
    } else out += "YA";
    if (ctor || f.isDestructor) out += "@";
    else out += resultType(resultOverride ? resultOverride : ft ? ft->inner : 0);
    out += ft ? paramList(ft) : std::string("X");
    return out + "Z";
}

std::string SemaMangler::variable(const sema::Symbol& v)
{
    if (v.isExternC) return v.name;
    // the back-reference table depends on the order of these calls: one statement each
    std::string out = "?";
    out += name(v.name);
    out += scopesOf(v.scope);
    out += "@";
    if (v.isMember) out += v.access == cxx::AccessPrivate ? "0" : v.access == cxx::AccessProtected ? "1" : "2";
    else out += "3";
    out += storageType(v.type);
    return out;
}

// the symbol of the .CRT$XCU entry of a COMDAT variable's dynamic initializer, as cl spells it:
// ??made$initializer$@?$Box@H@@2P6AXXZEA@@3P6AXXZEA (the name with "$initializer$", the scopes, the member's access, a
// pointer to a function)
std::string SemaMangler::initializerEntry(const sema::Symbol& v)
{
    std::string out = "??" + v.name + "$initializer$@";
    out += scopesOf(v.scope);
    out += "@";
    out += v.isMember ? (v.access == cxx::AccessPrivate ? "0" : v.access == cxx::AccessProtected ? "1" : "2") : "3";
    return out + "P6AXXZEA@@3P6AXXZEA";
}

// as cl spells a static local: the name, then a scope made of the block's number and the function's own
// decorated name (whose names join this name's back-reference table; a C function is "?name@@9"), then
// storage class 4 and the type as for a global
std::string SemaMangler::staticLocal(const sema::Symbol& v, const sema::Symbol& fn, long discriminator)
{
    std::string out = "?";
    out += name(v.name);
    out += "?";
    out += number(discriminator);
    std::string f = function(fn);
    if (f.empty() || f[0] != '?') f = "?" + f + "@@9";
    out += "?" + f + "@";
    out += "4";
    out += storageType(v.type);
    return out;
}

// cl's guard of the index-th dynamically initialised static local of fn: an int "$TSS<index>" in the same block
std::string SemaMangler::staticGuard(const sema::Symbol& fn, long discriminator, int index)
{
    std::ostringstream id; id << "$TSS" << index;
    std::string out = "?";
    out += name(id.str());
    out += "?";
    out += number(discriminator);
    std::string f = function(fn);
    if (f.empty() || f[0] != '?') f = "?" + f + "@@9";
    out += "?" + f + "@";
    out += "4HA";
    return out;
}

// the type part of a variable's name: "HA", "PEBDEB", "PAPEAUItem@1@A", ...
std::string SemaMangler::storageType(const sema::Type* t)
{
    std::string out;
    if (t && (t->kind == sema::Type::Pointer || t->kind == sema::Type::Reference)) {
        // "int* p" -> "PEAHEA": the pointer, then its own __ptr64 and the pointee's qualifiers again
        out += type(t);
        out += "E";
        out += cvCode(t->inner);
    } else if (t && t->kind == sema::Type::MemberPointer) {
        // "int P::* pm" -> "PEQP@@HEQ1@": the pointee's qualifier is its class again (back-referenced)
        out += type(t);
        out += "E";
        const sema::Type* in = t->inner;
        const bool fn = in && in->kind == sema::Type::Function;
        out += !fn && in && in->isConst ? (in->isVolatile ? "T" : "R") : (!fn && in && in->isVolatile ? "S" : "Q");
        out += memberClass(t);
    } else if (t && t->kind == sema::Type::Array) {
        // "T a[4]" -> "PA" T "A": a pointer to the element without __ptr64, the outer dimension is dropped;
        // "int a[2][3]" -> "PAY02HA"
        const sema::Type* first = t->inner;
        const sema::Type* elem = first;
        while (elem && elem->kind == sema::Type::Array) elem = elem->inner;
        bool c = (elem && elem->isConst) || (first && first->isConst), vol = (elem && elem->isVolatile) || (first && first->isVolatile);
        out += c ? (vol ? "S" : "Q") : (vol ? "R" : "P");
        if (first && first->kind == sema::Type::Array) {
            out += "AY";
            long dims = 0;
            for (const sema::Type* e = first; e && e->kind == sema::Type::Array; e = e->inner) ++dims;
            out += number(dims);
            for (const sema::Type* e = first; e && e->kind == sema::Type::Array; e = e->inner) out += number(e->arraySize < 0 ? 0 : e->arraySize);
            out += templateArgType(elem);
            out += "A";
        } else {
            out += cvCode(first);
            out += type(first);
            out += cvCode(first);
        }
    } else {
        out += type(t);
        out += cvCode(t);
    }
    return out;
}

// a class with several vftables names each after the bases that tell it from the others (IrGen::vfptrPaths):
// ??_7D@@6BPB@@@, ??_7G@@6BQ1@@@; one vftable is 6B@
std::string SemaMangler::vtable(const sema::Symbol& c, const std::vector<const sema::Symbol*>& path)
{
    std::string out = "??_7";
    out += symbolName(&c);
    out += scopesOf(c.scope, true);
    out += "@6B";
    for (size_t p = 0; p < path.size(); ++p) {
        out += symbolName(path[p]);
        out += scopesOf(path[p]->scope, true);
        out += "@";
    }
    return out + "@";
}

// the name in a type descriptor: "." and the type's code, a class or enum with its cv-qualifier letter (".?AUD@@",
// ".?AVstring@std@@", ".H", ".PEAUD@@"); typeid strips the top-level qualifiers and references (5.2.8)
std::string SemaMangler::rttiTypeName(const sema::Type* t)
{
    if (t && t->kind == sema::Type::Reference) t = t->inner;
    if (t && (t->kind == sema::Type::Class || t->kind == sema::Type::Enum)) return ".?A" + type(t);
    if (t && t->kind == sema::Type::Array) {
        // an array type as cl's descriptor spells it: "$$BY" + the rank + each dimension + the element (".$$BY02H" for
        // int[3], ".$$BY113US@@" for S[2][4]), which undecorates to "int [3]"; a parameter's array would be a pointer
        std::vector<long> dims;
        const sema::Type* e = t;
        while (e && e->kind == sema::Type::Array) { dims.push_back(e->arraySize > 0 ? e->arraySize : 0); e = e->inner; }
        std::string s = ".$$BY" + number(static_cast<long>(dims.size()));
        for (size_t i = 0; i < dims.size(); ++i) s += number(dims[i]);
        return s + type(e);
    }
    return "." + type(t);
}

// cl's base class descriptor: ??_R1<mdisp><pdisp><vdisp><attributes><class>@8, "D::`RTTI Base Class Descriptor at
// (16,-1,0,64)'" is ??_R1BA@?0A@EA@D@@8
std::string SemaMangler::baseClassDescriptor(const sema::Symbol& c, long mdisp, long pdisp, long vdisp, long attributes)
{
    std::string out = "??_R1";
    out += number(mdisp);
    out += number(pdisp);
    out += number(vdisp);
    out += number(attributes);
    return out + classScoped(c) + "8";
}

// a vbtable, named by the bases that tell it from the class's others: ??_8D@@7B@, ??_8VX@@7BVL@@@, ??_8R@@7BB@@P@@@
// cl's virtual displacement map of a pointer-to-member conversion from a class into a derived one that does not
// share its vbptr (??_KW@@$CVW@@): the base, then $C and the derived class
std::string SemaMangler::displacementMap(const sema::Symbol& from, const sema::Symbol& to)
{
    std::string out = "??_K";
    out += symbolName(&from);
    out += scopesOf(from.scope, true);
    out += "@$C";
    out += symbolName(&to);
    out += scopesOf(to.scope, true);
    return out + "@";
}

std::string SemaMangler::vbtable(const sema::Symbol& c, const std::vector<sema::Symbol*>& path)
{
    std::string out = "??_8";
    out += symbolName(&c);
    out += scopesOf(c.scope, true);
    out += "@7B";
    for (size_t p = 0; p < path.size(); ++p) {
        out += symbolName(path[p]);
        out += scopesOf(path[p]->scope, true);
        out += "@";
    }
    return out + "@";
}

std::string SemaMangler::vcallThunk(const sema::Symbol& c, long slotOffset)
{
    std::string out = "??_9";
    out += symbolName(&c);
    out += scopesOf(c.scope, true);
    out += "@$B";
    out += number(slotOffset);
    return out + "AA";
}

} // namespace

std::string msvcMangleSymbol(const sema::Symbol& fn) { SemaMangler m; return m.function(fn); }
unsigned long g_mangleUnitId = 0;
void setMangleUnitId(unsigned long id) { g_mangleUnitId = id; }

std::string msvcMangleVariableSymbol(const sema::Symbol& v) { SemaMangler m; return m.variable(v); }
// cl's dynamic initializer of a COMDAT variable ("void `dynamic initializer for 'Box<int>::made''(void)"), ??__E<the
// variable's name without its '?'>@@YAXXZ, and the symbol of its .CRT$XCU entry, ??made$initializer$@?$Box@H@@2P6AXXZEA@@3P6AXXZEA
std::string msvcMangleDynamicInitializer(const sema::Symbol& v) { SemaMangler m; return "??__E" + m.variable(v) + "@@YAXXZ"; }
std::string msvcMangleInitializerEntry(const sema::Symbol& v) { SemaMangler m; return m.initializerEntry(v); }
std::string msvcMangleStaticLocal(const sema::Symbol& v, const sema::Symbol& fn, long discriminator) { SemaMangler m; return m.staticLocal(v, fn, discriminator); }
std::string msvcMangleVtableSymbol(const sema::Symbol& c, const std::vector<const sema::Symbol*>& path) { SemaMangler m; return m.vtable(c, path); }
std::string msvcMangleVbtableSymbol(const sema::Symbol& c, const std::vector<sema::Symbol*>& path) { SemaMangler m; return m.vbtable(c, path); }
std::string msvcMangleDisplacementMap(const sema::Symbol& from, const sema::Symbol& to) { SemaMangler m; return m.displacementMap(from, to); }
std::string msvcMangleThunkSymbol(const sema::Symbol& fn, long adjust) { SemaMangler m; return m.function(fn, adjust); }
std::string msvcMangleVcallThunk(const sema::Symbol& c, long slotOffset) { SemaMangler m; return m.vcallThunk(c, slotOffset); }
std::string msvcRttiTypeName(const sema::Type* t) { SemaMangler m; return m.rttiTypeName(t); }
std::string msvcMangleTypeDescriptor(const sema::Type* t) { SemaMangler m; return m.typeDescriptor(t); }
std::string msvcMangleClassHierarchy(const sema::Symbol& c) { SemaMangler m; return m.classHierarchy(c); }
std::string msvcMangleBaseClassArray(const sema::Symbol& c) { SemaMangler m; return m.baseClassArray(c); }
std::string msvcMangleBaseClassDescriptor(const sema::Symbol& c, long mdisp, long pdisp, long vdisp, long attributes) { SemaMangler m; return m.baseClassDescriptor(c, mdisp, pdisp, vdisp, attributes); }
std::string msvcMangleCompleteObjectLocator(const sema::Symbol& c, const std::vector<const sema::Symbol*>& path) { SemaMangler m; return m.completeObjectLocator(c, path); }
std::string msvcMangleReturnThunk(const sema::Symbol& fn, const sema::Symbol& root) { SemaMangler m; return m.function(fn, 0, root.type && root.type->kind == sema::Type::Function ? root.type->inner : 0); }
std::string msvcMangleReturnAdjustorThunk(const sema::Symbol& fn, const sema::Symbol& root, long adjust) { SemaMangler m; return m.function(fn, adjust, root.type && root.type->kind == sema::Type::Function ? root.type->inner : 0, false, 0, true); }
std::string msvcMangleVtordispThunk(const sema::Symbol& fn, long adjust) { SemaMangler m; return m.function(fn, adjust, 0, true); }
std::string msvcMangleReturnVtordispThunk(const sema::Symbol& fn, const sema::Symbol& root, long adjust) { SemaMangler m; return m.function(fn, adjust, root.type && root.type->kind == sema::Type::Function ? root.type->inner : 0, true); }
std::string msvcMangleVtordispexThunk(const sema::Symbol& fn, long vbptrOffset, long entryOffset, long nonVirtual) { SemaMangler m; const long x[3] = { vbptrOffset, entryOffset, nonVirtual }; return m.function(fn, 0, 0, true, x); }
std::string msvcMangleStaticGuard(const sema::Symbol& fn, long discriminator, int index) { SemaMangler m; return m.staticGuard(fn, discriminator, index); }

} // namespace cppparse
