// SemaExpr.cpp - version 6 stage 3: expressions, conversions, overload resolution, statements, bodies.
#include "Sema.hpp"
#include "Literal.hpp"
#include "PathUtil.hpp"

#include <cstdlib>
#include <sstream>

namespace cppparse {
namespace sema {

// the value of an integer literal (decimal, 0x hexadecimal or 0 octal, suffixes ignored), modulo 2^64;
// *overflow is set when it does not fit in 64 bits
unsigned long long integerLiteralValue(const std::string& text, bool* overflow)
{
    int radix = 10;
    size_t i = 0;
    if (text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { radix = 16; i = 2; }
    else if (text.size() > 1 && text[0] == '0') { radix = 8; i = 1; }
    unsigned long long v = 0;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
        if (d >= radix) break;
        if (overflow && v > (0xFFFFFFFFFFFFFFFFULL - static_cast<unsigned long long>(d)) / static_cast<unsigned long long>(radix)) *overflow = true;
        v = v * static_cast<unsigned long long>(radix) + static_cast<unsigned long long>(d);
    }
    return v;
}

namespace {

std::string numText(long v) { std::ostringstream s; s << v; return s.str(); }

bool isNullConstant(const cxx::Expr* e)
{
    return e && e->semaConst && e->semaValue == 0 && e->semaType && e->semaType->isIntegral() && e->semaType->kind != Type::Enum;
}

// a folded constant converted to type t (semaValue holds 32 bits: wider types keep the bit pattern)
long fitTo(long v, const Type* t)
{
    switch (t->kind) {
    case Type::Bool: return v != 0;
    case Type::Char: case Type::SChar: return static_cast<signed char>(v);
    case Type::UChar: return static_cast<unsigned char>(v);
    case Type::Short: return static_cast<short>(v);
    case Type::UShort: case Type::WChar: return static_cast<unsigned short>(v);
    default: return v;
    }
}

// an unsigned 32-bit constant beyond 2^31 cast to a 64-bit type: its value leaves semaValue's sign-extended 32
// bits ((unsigned long long)(unsigned)-1.0 is FFFFFFFFh, not 2^64-1), so the cast is left to IrGen
bool widensUnsigned(const Type* from, const Type* to, long v)
{
    while (from && from->kind == Type::Reference) from = from->inner;
    if (!from || v >= 0 || (to->kind != Type::LongLong && to->kind != Type::ULongLong)) return false;
    return from->kind == Type::UInt || from->kind == Type::ULong;
}

// the IR sign-extends the 32 bits of semaValue, so an operation of a 64-bit type is folded only while its
// result stays in that range; otherwise it is left to run (1ULL << 40 was folded to 1 << 8)
void foldWide(cxx::Expr* e, const std::string& op, long a, long b, bool isUnsigned)
{
    const long long x = a, y = b;
    const unsigned long long ux = static_cast<unsigned long long>(x), uy = static_cast<unsigned long long>(y);
    unsigned long long r = 0;
    bool ok = true;
    if (op == "+") r = ux + uy;
    else if (op == "-") r = ux - uy;
    else if (op == "*") r = ux * uy;
    else if (op == "/" || op == "%") {
        if (y == 0 || (!isUnsigned && y == -1)) ok = false;
        else if (isUnsigned) r = op == "/" ? ux / uy : ux % uy;
        else r = static_cast<unsigned long long>(op == "/" ? x / y : x % y);
    }
    else if (op == "<<" || op == ">>") {
        if (y < 0 || y > 63) ok = false;
        else if (op == "<<") r = ux << y;
        else r = isUnsigned ? ux >> y : static_cast<unsigned long long>(x >> y);
    }
    else if (op == "&") r = ux & uy;
    else if (op == "|") r = ux | uy;
    else if (op == "^") r = ux ^ uy;
    else ok = false;
    const long long s = static_cast<long long>(r);
    e->semaConst = ok && static_cast<long long>(static_cast<long>(s)) == s;
    e->semaValue = e->semaConst ? static_cast<long>(s) : 0;
}

bool isStringInit(const Type* array, const cxx::Initializer& init)
{
    if (!array || array->kind != Type::Array || !array->inner) return false;
    const Type::Kind k = array->inner->kind;
    if (k != Type::Char && k != Type::SChar && k != Type::UChar && k != Type::WChar) return false;
    return (init.kind == cxx::Initializer::Assign || init.kind == cxx::Initializer::Parens) && init.args.size() == 1
        && init.args[0]->kind == cxx::Expr::StringLit;
}

bool aggregateType(const Type* t)
{
    if (!t) return false;
    if (t->kind == Type::Reference) t = t->inner;
    if (t->kind == Type::Array) return true;
    if (t->kind != Type::Class || !t->decl || !t->decl->isDefinition) return false;
    const Symbol* c = t->decl;
    return !c->hasUserCtor && c->bases.empty() && !c->isPolymorphic;
}

// how many initializers an element of type t takes when its braces are elided
long scalarSlots(const Type* t, int depth)
{
    if (!t || depth > 32 || !aggregateType(t)) return 1;
    if (t->kind == Type::Array) return t->arraySize > 0 ? t->arraySize * scalarSlots(t->inner, depth + 1) : 1;
    long n = 0;
    const std::vector<Symbol*>& f = t->decl->fields;
    for (size_t i = 0; i < f.size(); ++i) { n += scalarSlots(f[i]->type, depth + 1); if (t->decl->isUnion) break; }
    return n > 0 ? n : 1;
}

} // namespace

bool Sema::isAggregate(const Type* t)
{
    return aggregateType(t);
}

// counted on the syntax alone, so that the size is known where the variable is declared
long Sema::braceCount(const Type* t, const std::vector<cxx::Expr*>& items)
{
    const long slots = aggregateType(t->inner) ? scalarSlots(t->inner, 0) : 1;
    long n = 0;
    for (size_t i = 0; i < items.size(); ++n) {
        if (items[i]->kind == cxx::Expr::InitList || slots == 1) ++i;
        else i += static_cast<size_t>(slots);
    }
    return n;
}

const Type* Sema::completedArray(const Type* t, const cxx::Initializer& init)
{
    if (!t || t->kind != Type::Array || t->arraySize >= 0) return t;
    if (isStringInit(t, init) || (init.kind == cxx::Initializer::Brace && init.args.size() == 1 && literalForCharArray(t, init.args[0])))
        return m_types.array(t->inner, stringLiteralLength(init.args[0]->text) + 1);   // char s[] = "...", char s[] = { "..." }
    if (init.kind != cxx::Initializer::Brace) return t;
    return m_types.array(t->inner, braceCount(t, init.args));
}

// the operand type of sizeof (references already removed) must be complete (C++98 5.3.3; cl C2027, C2070): a class
// only declared, a class whose own member declarations are still being read, void or a function type is an error.
// A defined class that is not laid out yet is laid out here, so that its size is the real one
bool Sema::completeForSizeof(const Type* t, Scope* in, const cxx::Loc& loc)
{
    while (t && t->kind == Type::Array) t = t->inner;
    if (!t) return true;
    if (t->kind == Type::Void) { error(loc, "S113", "sizeof of void"); return false; }
    if (t->kind == Type::Function) { error(loc, "S113", "sizeof of a function type"); return false; }
    if (t->kind != Type::Class || !t->decl || t->decl->layoutDone || t->decl->isDependentContext) return true;
    Symbol* cls = t->decl;
    bool beingDefined = false;
    for (Scope* s = in; s && !beingDefined; s = s->parent) if (s->kind == Scope::Class && s->owner == cls) beingDefined = true;
    if (!cls->isDefinition || beingDefined) { error(loc, "S113", "sizeof of incomplete class '" + cls->qualifiedName() + "'"); return false; }
    layoutClass(cls);
    return true;
}

// the initializers from items[pos] on initialise an object of aggregate type t
cxx::Expr* Sema::braceInit(const Type* t, std::vector<cxx::Expr*>& items, size_t& pos, Scope* in, const cxx::Loc& loc, long* count)
{
    cxx::Expr* list = nodeArena().expr(cxx::Expr::InitList, loc);
    const Type* u = m_types.unqualified(m_types.unref(t));
    list->semaType = u;
    long n = 0;
    if (u->kind == Type::Array) {
        for (; pos < items.size() && (u->arraySize < 0 || n < u->arraySize); ++n) list->kids.push_back(braceElement(u->inner, items, pos, in));
    } else if (u->kind == Type::Class && u->decl) {
        const std::vector<Symbol*>& f = u->decl->fields;
        for (size_t i = 0; i < f.size() && pos < items.size(); ++i) {
            list->kids.push_back(braceElement(f[i]->type, items, pos, in));
            if (u->decl->isUnion) break;
        }
    }
    if (count) *count = n;
    return list;
}

// a string literal as the initializer of a char (wchar_t) array element of a brace list: "struct N { char n[6]; } g =
// { "ab" }", also inside its own braces (8.5.2); it must fit with its terminator
bool Sema::literalForCharArray(const Type* u, cxx::Expr* it)
{
    if (!it || it->kind != cxx::Expr::StringLit || !u || u->kind != Type::Array || !u->inner) return false;
    const Type::Kind k = m_types.unqualified(u->inner)->kind;
    return k == Type::Char || k == Type::SChar || k == Type::UChar || k == Type::WChar;
}

cxx::Expr* Sema::braceElement(const Type* t, std::vector<cxx::Expr*>& items, size_t& pos, Scope* in)
{
    cxx::Expr* it = items[pos];
    const Type* u = m_types.unqualified(m_types.unref(t));
    if (it->kind == cxx::Expr::InitList) {
        ++pos;
        if (isAggregate(u) && !(it->kids.size() == 1 && literalForCharArray(u, it->kids[0]))) {
            size_t p = 0;
            cxx::Expr* list = braceInit(u, it->kids, p, in, it->loc, 0);
            if (p < it->kids.size()) error(it->loc, "S105", "too many initialisers for " + u->text());
            return list;
        }
        if (it->kids.size() != 1) { error(it->loc, "S105", "a braced initialiser of " + u->text() + " needs exactly one element"); return it; }
        it = it->kids[0];
    } else {
        if (it->kind != cxx::Expr::Error && !it->semaType) expr(it, in);
        if (isAggregate(u) && it->kind != cxx::Expr::Error && m_types.unqualified(valueType(it)) != u && !literalForCharArray(u, it)) {
            // braces elided: the element takes as many initializers as it needs
            size_t before = pos;
            cxx::Expr* list = braceInit(u, items, pos, in, it->loc, 0);
            if (pos == before) { error(it->loc, "S105", "cannot initialise an element of type " + u->text()); ++pos; }
            return list;
        }
        ++pos;
    }
    if (it->kind == cxx::Expr::Error) return it;
    if (!it->semaType) expr(it, in);
    if (literalForCharArray(u, it)) {
        const Type* lt = m_types.unref(it->semaType);
        if (lt && lt->kind == Type::Array && m_types.sizeOf(lt->inner) != m_types.sizeOf(u->inner)) error(it->loc, "S105", "cannot initialise an element of type " + u->text() + " from " + lt->text());
        else if (u->arraySize >= 0 && stringLiteralLength(it->text) + 1 > u->arraySize) error(it->loc, "S105", "string literal too long for an element of type " + u->text());
        return it;
    }
    if (u->kind == Type::Dependent || u->kind == Type::Error) return it;
    Conversion c = convert(it, u, in);
    const Type* vt = valueType(it);
    if (c.rank == Conversion::None && vt->kind != Type::Error && vt->kind != Type::Dependent) error(it->loc, "S105", "cannot initialise an element of type " + u->text() + " from " + vt->text());
    return converted(it, c);
}

// "T x = { ... }": the declarator's arguments become the fully braced elements of x
void Sema::braceDeclInit(Symbol* var, cxx::Initializer& init, Scope* in)
{
    const Type* t = m_types.unref(var->type);
    if (t->isDependent() || t->kind == Type::Error) {
        for (size_t a = 0; a < init.args.size(); ++a) if (init.args[a]->kind != cxx::Expr::Error && init.args[a]->kind != cxx::Expr::InitList) expr(init.args[a], in);
        return;
    }
    if (!isAggregate(t)) {
        if (init.args.size() != 1 || init.args[0]->kind == cxx::Expr::InitList) { if (!init.args.empty()) error(var->loc, "S105", "brace initialiser for '" + var->name + "' of non-aggregate type " + t->text()); return; }
        size_t pos = 0;
        init.args[0] = braceElement(t, init.args, pos, in);
        var->init = init.args[0];
        return;
    }
    if (init.args.size() == 1 && literalForCharArray(t, init.args[0])) {
        // "char s[4] = { "ab" };": the literal in braces initialises the array as the unbraced form does (8.5.2/1)
        if (!init.args[0]->semaType) expr(init.args[0], in);
        if (t->arraySize >= 0 && stringLiteralLength(init.args[0]->text) + 1 > t->arraySize) error(var->loc, "S105", "string literal too long for '" + var->name + "'");
        init.kind = cxx::Initializer::Assign;
        var->init = init.args[0];
        if (t->arraySize < 0) var->type = m_types.array(t->inner, stringLiteralLength(init.args[0]->text) + 1);
        return;
    }
    size_t pos = 0;
    long n = 0;
    cxx::Expr* list = braceInit(t, init.args, pos, in, var->loc, &n);
    if (pos < init.args.size()) error(var->loc, "S105", "too many initialisers for '" + var->name + "'");
    init.args = list->kids;
    var->init = init.args.empty() ? 0 : init.args[0];
    if (t->kind == Type::Array && t->arraySize < 0) var->type = m_types.array(t->inner, n);
}

const Type* Sema::valueType(cxx::Expr* e)
{
    if (!e || !e->semaType) return m_types.error();
    const Type* t = e->semaType;
    if (t->kind == Type::Reference) t = t->inner;
    if (t->kind == Type::Array || t->kind == Type::Function) return m_types.decay(t);
    if (t->kind == Type::Class) return t;
    return m_types.unqualified(t);
}

bool Sema::constantValue(cxx::Expr* e, long& out)
{
    if (!e) return false;
    if (!e->semaType) expr(e, m_prog.global);
    if (e->semaConst) { out = e->semaValue; return true; }
    return false;
}

Conversion Sema::convertType(const Type* from, bool lvalue, bool nullConstant, const Type* to, Scope* in, bool allowUser)
{
    Conversion c;
    c.target = to;
    if (!from || !to) return c;
    if (from->kind == Type::Error || to->kind == Type::Error || from->kind == Type::Dependent || to->kind == Type::Dependent) { c.rank = Conversion::Exact; return c; }
    if (to->kind == Type::Reference) {
        const Type* ref = to->inner;
        const Type* src = from->kind == Type::Reference ? from->inner : from;
        bool srcLvalue = lvalue || from->kind == Type::Reference;
        const Type* ru = m_types.unqualified(ref);
        const Type* su = m_types.unqualified(src);
        bool cvOk = (!src->isConst || ref->isConst) && (!src->isVolatile || ref->isVolatile);
        // the cv-qualifiers of an array are those of its elements (8.3.4): double[4] binds to const double (&)[4]
        bool sameArray = false;
        if (su->kind == Type::Array && ru->kind == Type::Array) {
            const Type* se = su; const Type* re = ru;
            while (se->kind == Type::Array && re->kind == Type::Array && se->arraySize == re->arraySize) { se = se->inner; re = re->inner; }
            if (se->kind != Type::Array && re->kind != Type::Array && m_types.unqualified(se) == m_types.unqualified(re)) {
                sameArray = true;
                cvOk = cvOk && (!se->isConst || re->isConst) && (!se->isVolatile || re->isVolatile);
            }
        }
        if (ru == su || sameArray || (su->kind == Type::Array && ru->kind == Type::Array && su->inner == ru->inner)) {
            if (srcLvalue && cvOk) { c.rank = Conversion::Exact; c.qualification = ru != su || src->isConst != ref->isConst; return c; }
            if (ref->isConst && !ref->isVolatile) { c.rank = Conversion::Exact; return c; }      // const T& binds a temporary
            return c;
        }
        if (ru->kind == Type::Class && su->kind == Type::Class && su->decl && ru->decl && derivesFrom(su->decl, ru->decl, 0) && baseSubobjects(su->decl, ru->decl) <= 1) {
            if (srcLvalue && cvOk) { c.rank = Conversion::Standard; c.derivedToBase = true; return c; }
            if (ref->isConst) { c.rank = Conversion::Standard; c.derivedToBase = true; return c; }
            return c;
        }
        if (ref->isConst && !ref->isVolatile) {
            // a temporary of the referenced type made by a conversion
            Conversion inner = convertType(src, srcLvalue, nullConstant, ru, in, allowUser);
            if (inner.rank != Conversion::None) { inner.target = to; if (inner.rank < Conversion::Standard) inner.rank = Conversion::Standard; return inner; }
        }
        return c;
    }
    const Type* src = from->kind == Type::Reference ? from->inner : from;
    if (src->kind == Type::Array || src->kind == Type::Function) src = m_types.decay(src);
    src = m_types.unqualified(src);
    const Type* dst = m_types.unqualified(to);
    if (src == dst) { c.rank = Conversion::Exact; return c; }
    if (dst->kind == Type::Class) {
        if (src->kind == Type::Class && src->decl && dst->decl && derivesFrom(src->decl, dst->decl, 0) && baseSubobjects(src->decl, dst->decl) <= 1) { c.rank = Conversion::Standard; c.derivedToBase = true; return c; }
        if (!allowUser || !dst->decl || !dst->decl->members) return c;
        // converting constructor
        std::vector<Symbol*> ctors;
        lookupMember(dst->decl, dst->decl->ctorName(), ctors);
        for (size_t i = 0; i < ctors.size(); ++i) {
            Symbol* f = ctors[i];
            if (f->kind != Symbol::Function || !f->isConstructor || f->isExplicit || f->params.empty() || f->requiredParams > 1) continue;
            const Type* p0 = f->params[0]->type;
            if (f->isImplicit) continue;
            Conversion inner = convertType(from, lvalue, nullConstant, p0, in, false);
            if (inner.rank != Conversion::None) { c.rank = Conversion::UserDefined; c.userFunction = f; return c; }
        }
        if (src->kind == Type::Class && src->decl && src->decl->members) {
            // a conversion function of the source class (or of a base) to the target class, or to a class
            // derived from it (its result is then converted to the base)
            for (size_t i = 0; i < src->decl->members->order.size(); ++i) {
                Symbol* f = src->decl->members->order[i];
                if (f->kind != Symbol::Function || !f->isConversion || !f->type || !f->type->inner) continue;
                const Type* rt = m_types.unqualified(m_types.unref(f->type->inner));
                if (rt == dst || (rt->kind == Type::Class && rt->decl && derivesFrom(rt->decl, dst->decl, 0))) { c.rank = Conversion::UserDefined; c.userFunction = f; c.second = rt == dst ? Conversion::Exact : Conversion::Standard; return c; }
            }
            for (size_t b = 0; b < src->decl->bases.size(); ++b) {
                Conversion inner = convertType(m_types.classType(src->decl->bases[b].cls), lvalue, false, to, in, true);
                if (inner.rank == Conversion::UserDefined && inner.userFunction && inner.userFunction->isConversion) return inner;
            }
        }
        return c;
    }
    if (src->kind == Type::Class) {
        if (!allowUser || !src->decl || !src->decl->members) return c;
        // conversion operator to the target (or to something that converts to it): the one whose result converts
        // best (13.3.3/1: operator double() over operator int() for a double target)
        Conversion best;
        for (size_t i = 0; i < src->decl->members->order.size(); ++i) {
            Symbol* f = src->decl->members->order[i];
            if (f->kind != Symbol::Function || !f->isConversion || !f->type) continue;
            Conversion inner = convertType(f->type->inner, f->type->inner->kind == Type::Reference, false, to, in, false);
            if (inner.rank == Conversion::None) continue;
            if (!c.userFunction || inner.rank < best.rank || (inner.rank == best.rank && ((best.derivedToBase && !inner.derivedToBase) || (best.qualification && !inner.qualification && best.derivedToBase == inner.derivedToBase)))) { best = inner; c.rank = Conversion::UserDefined; c.userFunction = f; c.second = inner.rank; }
        }
        if (c.userFunction) return c;
        for (size_t b = 0; b < src->decl->bases.size(); ++b) {
            Conversion inner = convertType(m_types.classType(src->decl->bases[b].cls), lvalue, false, to, in, true);
            if (inner.rank == Conversion::UserDefined) return inner;
        }
        return c;
    }
    if (dst->kind == Type::Bool) {
        if (src->isArithmetic() || src->isPointer() || src->kind == Type::MemberPointer) { c.rank = Conversion::Standard; return c; }
        return c;
    }
    if (dst->isArithmetic()) {
        if (src->kind == Type::Enum) { c.rank = dst->kind == Type::Int ? Conversion::Promotion : Conversion::Standard; return c; }
        if (!src->isArithmetic()) return c;
        if (m_types.promoted(src) == dst && src != dst) { c.rank = Conversion::Promotion; return c; }
        c.rank = Conversion::Standard;
        return c;
    }
    if (dst->kind == Type::Enum) return c;
    if (dst->kind == Type::Pointer) {
        if (src->kind == Type::NullPtr || nullConstant) { c.rank = Conversion::Standard; return c; }
        if (src->kind != Type::Pointer) return c;
        const Type* sp = src->inner; const Type* dp = dst->inner;
        bool cvOk = (!sp->isConst || dp->isConst) && (!sp->isVolatile || dp->isVolatile);
        if (!cvOk) return c;
        const Type* spu = m_types.unqualified(sp); const Type* dpu = m_types.unqualified(dp);
        if (spu == dpu) { c.rank = Conversion::Exact; c.qualification = sp != dp; return c; }
        if (dpu->kind == Type::Void) { c.rank = Conversion::Standard; return c; }
        if (spu->kind == Type::Class && dpu->kind == Type::Class && spu->decl && dpu->decl && derivesFrom(spu->decl, dpu->decl, 0) && baseSubobjects(spu->decl, dpu->decl) <= 1) { c.rank = Conversion::Standard; c.derivedToBase = true; return c; }
        return c;
    }
    if (dst->kind == Type::MemberPointer) {
        if (src->kind == Type::NullPtr || nullConstant) { c.rank = Conversion::Standard; return c; }
        if (src->kind != Type::MemberPointer || !src->inner || !dst->inner) return c;
        // the same member type (a data member's with qualifiers added) of the same class, or of a class derived
        // from it (4.11)
        const Type* si = src->inner;
        const Type* di = dst->inner;
        const bool sameMember = si == di || (si->kind != Type::Function && di->kind != Type::Function && m_types.unqualified(si) == m_types.unqualified(di)
                                             && (!si->isConst || di->isConst) && (!si->isVolatile || di->isVolatile));
        if (!sameMember) return c;
        Symbol* sc = src->memberClass && src->memberClass->kind == Type::Class ? src->memberClass->decl : 0;
        Symbol* dc = dst->memberClass && dst->memberClass->kind == Type::Class ? dst->memberClass->decl : 0;
        if (sc == dc) { c.rank = Conversion::Exact; c.qualification = si != di; return c; }
        if (sc && dc && derivesFrom(dc, sc, 0)) { c.rank = Conversion::Standard; return c; }
        return c;
    }
    return c;
}

Conversion Sema::convert(cxx::Expr* from, const Type* to, Scope* in)
{
    if (!from->semaType) expr(from, in);
    Conversion c = convertType(from->semaType, from->semaLvalue, isNullConstant(from), to, in, true);
    if (c.rank == Conversion::None && to && from->semaType) {
        // 4.2/2: a string literal converts to char* (wchar_t* for L"..."), deprecated but valid C++98; for the ranking
        // it is an array-to-pointer conversion followed by a qualification conversion: f(const char*) beats f(char*)
        cxx::Expr* lit = from;
        while (lit->kind == cxx::Expr::Paren && lit->kids.size() == 1) lit = lit->kids[0];
        const Type* dst = m_types.unqualified(to);
        const Type* src = m_types.unref(from->semaType);
        if (lit->kind == cxx::Expr::StringLit && dst->kind == Type::Pointer && dst->inner && !dst->inner->isConst && src->kind == Type::Array && src->inner
            && m_types.unqualified(dst->inner) == m_types.unqualified(src->inner)) {
            c.rank = Conversion::Exact; c.qualification = true; c.target = to;
        }
        // a converting constructor template of the target class ("template <class U, class W> pair(const pair<U, W>&)":
        // pair<K, V> to map's pair<const K, V>), deduced from the argument (13.3.1.4; a reference target binds the
        // temporary it makes)
        const Type* cls = m_types.unqualified(m_types.unref(to));
        if (c.rank == Conversion::None && cls->kind == Type::Class && cls->decl && cls->decl->members && (to->kind != Type::Reference || to->inner->isConst)) {
            std::vector<Symbol*> ctors;
            lookupMember(cls->decl, cls->decl->ctorName(), ctors);
            for (size_t i = 0; i < ctors.size() && c.rank == Conversion::None; ++i) {
                if (ctors[i]->kind != Symbol::FunctionTemplate || ctors[i]->ownerClass != cls->decl) continue;
                std::vector<cxx::Expr*> one(1, from);
                Symbol* inst = instantiateFunctionTemplate(ctors[i], one, 0, in, from->loc, 0);
                if (!inst || inst->isExplicit || inst->params.empty() || inst->requiredParams > 1) continue;
                Conversion inner = convertType(from->semaType, from->semaLvalue, isNullConstant(from), inst->params[0]->type, in, false);
                if (inner.rank != Conversion::None) { c.rank = Conversion::UserDefined; c.userFunction = inst; c.target = to; }
            }
        }
    }
    return c;
}

const Type* Sema::resultOfCall(Symbol* fn)
{
    if (!fn || !fn->type || fn->type->kind != Type::Function) return m_types.error();
    return fn->type->inner;
}

namespace {

int compareConversions(const Conversion& a, const Conversion& b)
{
    // negative: a better, positive: b better, 0: indistinguishable
    if (a.rank != b.rank) return a.rank < b.rank ? -1 : 1;
    if (a.rank == Conversion::Standard || a.rank == Conversion::Exact) {
        // 13.3.3.2/4: a conversion to a base (Derived* to Base*, Derived to Base&) beats the other standard
        // conversions of the same rank, to void* and to bool
        if (a.derivedToBase != b.derivedToBase) return a.derivedToBase ? -1 : 1;
        if (a.qualification != b.qualification) return a.qualification ? 1 : -1;
    }
    // 13.3.3.2/3: two sequences through the same conversion function are ordered by the standard conversion
    // after it (operator int() to int beats operator int() to double)
    if (a.rank == Conversion::UserDefined && a.userFunction && a.userFunction == b.userFunction && a.second != b.second) return a.second < b.second ? -1 : 1;
    return 0;
}

} // namespace

Symbol* Sema::resolveOverload(const std::vector<Symbol*>& cands, const std::vector<cxx::Expr*>& args, size_t firstArg, cxx::Expr* thisExpr, Scope* in, const cxx::Loc& loc, const std::string& what, bool report, const std::vector<cxx::TemplateArg>* explicitArgs)
{
    size_t nargs = args.size() - firstArg;
    for (size_t a = firstArg; a < args.size(); ++a) if (!args[a]->semaType) expr(args[a], in);
    for (size_t a = firstArg; a < args.size(); ++a) if (args[a]->semaType && args[a]->semaType->kind == Type::Dependent) return cands.empty() ? 0 : cands[0];

    struct Viable { Symbol* fn; std::vector<Conversion> convs; bool exactThis; };
    std::vector<Viable> viable;
    // one entity reached twice is one candidate: "using namespace std; printf(...)" finds ::printf directly and
    // again through the using-declaration "using ::printf;" in std
    std::vector<Symbol*> all;
    for (size_t i = 0; i < cands.size(); ++i) {
        bool seen = false;
        for (size_t k = 0; k < all.size() && !seen; ++k) seen = all[k] == cands[i];
        if (!seen) all.push_back(cands[i]);
    }
    const size_t candidates = all.size();
    // function templates: deduce and instantiate, then take part like ordinary functions
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i]->kind == Symbol::FunctionTemplate) {
            Symbol* inst = instantiateFunctionTemplate(all[i], args, firstArg, in, loc, explicitArgs);
            if (inst) all.push_back(inst);
        }
    }
    for (size_t i = 0; i < all.size(); ++i) {
        Symbol* f = all[i];
        if (f->kind != Symbol::Function || !f->type || f->type->kind != Type::Function) continue;
        if (explicitArgs && !f->instantiatedFrom) continue;   // f<int>(x) names a template: no plain function is a candidate
        const Type* ft = f->type;
        if (nargs < static_cast<size_t>(f->requiredParams) || (nargs > ft->params.size() && !ft->variadic)) continue;
        Viable v; v.fn = f; v.exactThis = true;
        bool ok = true;
        if (thisExpr && f->ownerClass && !f->isStatic && !f->isConstructor) {
            const Type* ot = m_types.unref(thisExpr->semaType);
            if (ot && ot->kind == Type::Pointer) ot = ot->inner;
            if (ot && ot->isConst && !ft->constMethod) continue;
            if (ot && !ot->isConst && ft->constMethod) v.exactThis = false;
        }
        for (size_t a = 0; a < nargs && ok; ++a) {
            Conversion c;
            if (a < ft->params.size()) c = convert(args[firstArg + a], ft->params[a], in);
            else { c.rank = Conversion::Ellipsis; }
            if (c.rank == Conversion::None) ok = false;
            v.convs.push_back(c);
        }
        if (ok) viable.push_back(v);
    }
    bool anyErrorArg = false;
    for (size_t a = firstArg; a < args.size(); ++a) if (args[a]->semaType && args[a]->semaType->kind == Type::Error) anyErrorArg = true;
    if (viable.empty()) {
        if (report && !anyErrorArg) {
            std::string sig;
            for (size_t a = firstArg; a < args.size(); ++a) sig += (a > firstArg ? ", " : "") + (args[a]->semaType ? valueType(args[a])->text() : std::string("?"));
            error(loc, "S107", "no matching function for call to " + what + "(" + sig + ")" + (candidates == 0 ? "" : " among " + numText(static_cast<long>(candidates)) + " candidate(s)"));
        }
        return 0;
    }
    size_t best = 0;
    bool ambiguous = false;
    for (size_t i = 1; i < viable.size(); ++i) {
        int better = 0, worse = 0;
        for (size_t a = 0; a < nargs; ++a) {
            int c = compareConversions(viable[i].convs[a], viable[best].convs[a]);
            if (c < 0) ++better; else if (c > 0) ++worse;
        }
        if (viable[i].exactThis != viable[best].exactThis) { if (viable[i].exactThis) ++better; else ++worse; }
        // a non-template function beats a template specialisation
        bool iInst = viable[i].fn->instantiatedFrom != 0, bInst = viable[best].fn->instantiatedFrom != 0;
        if (better == 0 && worse == 0 && iInst != bInst) { if (bInst) better = 1; else worse = 1; }
        // two specialisations of different templates: the more specialised template wins (14.5.5.2)
        if (better == 0 && worse == 0 && iInst && bInst && viable[i].fn->instantiatedFrom != viable[best].fn->instantiatedFrom) {
            const bool iMore = atLeastAsSpecialized(viable[i].fn->instantiatedFrom, viable[best].fn->instantiatedFrom);
            const bool bMore = atLeastAsSpecialized(viable[best].fn->instantiatedFrom, viable[i].fn->instantiatedFrom);
            if (iMore && !bMore) better = 1; else if (bMore && !iMore) worse = 1;
        }
        if (better > 0 && worse == 0) { best = i; ambiguous = false; }
        else if (better == 0 && worse == 0) ambiguous = true;
    }
    if (ambiguous && !anyErrorArg) {
        if (report) error(loc, "S106", "call to " + what + " is ambiguous");
        ++m_stats.callsAmbiguous;
    }
    for (size_t a = 0; a < nargs; ++a) {
        if (report && a < viable[best].fn->type->params.size()) narrowingWarning(args[firstArg + a], viable[best].fn->type->params[a], "argument");
        // an argument converted through a conversion operator is wrapped in place (the vector is the call's own operand list)
        cxx::Expr* w = converted(args[firstArg + a], viable[best].convs[a]);
        if (w != args[firstArg + a]) const_cast<std::vector<cxx::Expr*>&>(args)[firstArg + a] = w;
    }
    promoteVariadic(const_cast<std::vector<cxx::Expr*>&>(args), firstArg, viable[best].fn->type);
    ++m_stats.callsResolved;
    return viable[best].fn;
}

namespace {

// the parameter's written type names a template parameter of tscope that is not among the fixed ones (given
// explicitly on the call): only then is there anything to deduce from the argument (14.8.2.1); a qualified name
// (T::type) is a non-deduced context
bool specMentionsOpen(const cxx::DeclSpec& spec, const Scope* tscope, const std::set<std::string>& fixed);
bool declaratorMentionsOpen(const cxx::Declarator* d, const Scope* tscope, const std::set<std::string>& fixed)
{
    if (!d) return false;
    for (size_t i = 0; i < d->parts.size(); ++i)
        for (size_t k = 0; k < d->parts[i].params.size(); ++k)
            if (d->parts[i].params[k] && (specMentionsOpen(d->parts[i].params[k]->spec, tscope, fixed) || declaratorMentionsOpen(d->parts[i].params[k]->decl, tscope, fixed))) return true;
    return false;
}
bool specMentionsOpen(const cxx::DeclSpec& spec, const Scope* tscope, const std::set<std::string>& fixed)
{
    if (spec.kind != cxx::DeclSpec::Named || !tscope) return false;
    if (spec.name.parts.size() == 1 && !spec.name.global && !spec.name.parts[0].hasTemplateArgs) {
        const std::vector<Symbol*>* v = tscope->find(spec.name.parts[0].ident);
        if (v) for (size_t k = 0; k < v->size(); ++k) if ((*v)[k]->kind == Symbol::TemplateParam) return !fixed.count(spec.name.parts[0].ident);
        return false;
    }
    if (spec.name.parts.size() != 1) return false;
    const cxx::NameComponent& c = spec.name.parts[0];
    for (size_t k = 0; k < c.templateArgs.size(); ++k) {
        const cxx::TemplateArg& ta = c.templateArgs[k];
        if (ta.type && (specMentionsOpen(ta.type->spec, tscope, fixed) || declaratorMentionsOpen(ta.type->decl, tscope, fixed))) return true;
        if (ta.expr && ta.expr->kind == cxx::Expr::Name && ta.expr->name.parts.size() == 1) {
            const std::vector<Symbol*>* v = tscope->find(ta.expr->name.parts[0].ident);
            if (v) for (size_t q = 0; q < v->size(); ++q) if ((*v)[q]->kind == Symbol::TemplateParam && !fixed.count(ta.expr->name.parts[0].ident)) return true;
        }
    }
    return false;
}

} // namespace

bool Sema::deduce(const cxx::Param* p, const Type* argType, Scope* tscope, std::map<std::string, const Type*>& bindings, std::map<std::string, long>& ints)
{
    // nothing to deduce: the parameter's type is fixed by the explicit arguments (or mentions no parameter); the
    // argument must convert to the substituted type later
    if (!m_fixedTemplateParams.empty() && !specMentionsOpen(p->spec, tscope, m_fixedTemplateParams) && !declaratorMentionsOpen(p->decl, tscope, m_fixedTemplateParams)) return true;
    // walks the parameter's declarator against the argument type
    const Type* a = argType;
    if (a->kind == Type::Reference) a = a->inner;
    const cxx::Declarator* d = p->decl;
    std::vector<cxx::DeclaratorPart> parts;
    if (d) parts = d->parts;
    // reference parameters bind the argument as is (arrays stay arrays); value parameters see the decayed type
    bool byRef = !parts.empty() && parts.back().kind == cxx::DeclaratorPart::Reference;
    if (!byRef) { if (a->kind == Type::Array || a->kind == Type::Function) a = m_types.decay(a); a = m_types.unqualified(a); }
    // peel the declarator from the outside in
    for (size_t i = parts.size(); i > 0; --i) {
        const cxx::DeclaratorPart& pt = parts[i - 1];
        if (pt.kind == cxx::DeclaratorPart::Paren) continue;
        if (pt.kind == cxx::DeclaratorPart::Reference) { if (a->kind == Type::Reference) a = a->inner; continue; }
        if (pt.kind == cxx::DeclaratorPart::Pointer) {
            if (a->kind != Type::Pointer) return false;
            if (m_strictDeduce && ((pt.isConst && !a->isConst) || (pt.isVolatile && !a->isVolatile))) return false;
            a = a->inner;
            continue;
        }
        if (pt.kind == cxx::DeclaratorPart::MemberPointer) {
            // R (T::*)(A): the class deduces from the member pointer's class, the member's type from its target
            if (a->kind != Type::MemberPointer || !a->memberClass || !a->inner) return false;
            const cxx::QualifiedName& mc = pt.memberClass;
            bool classParam = false;
            if (mc.parts.size() == 1 && !mc.global && tscope) { const std::vector<Symbol*>* v = tscope->find(mc.parts[0].ident); if (v) for (size_t k = 0; k < v->size(); ++k) if ((*v)[k]->kind == Symbol::TemplateParam) classParam = true; }
            if (classParam && !m_fixedTemplateParams.count(mc.parts[0].ident)) {
                std::map<std::string, const Type*>::iterator it = bindings.find(mc.parts[0].ident);
                const Type* bound = m_types.unqualified(a->memberClass);
                if (it == bindings.end()) bindings[mc.parts[0].ident] = bound;
                else if (it->second != bound) return false;
            } else if (!classParam) {
                const Type* ct = namedType(mc, tscope ? tscope : m_prog.global, "", p->loc);
                if (!ct || m_types.unqualified(ct) != m_types.unqualified(a->memberClass)) return false;
            }
            a = a->inner;
            continue;
        }
        if (pt.kind == cxx::DeclaratorPart::Function) {
            // R (*)(A, B): the parameters deduce from the function type's, the rest from its result (14.8.2.4); a
            // member function's const must match its pointer's
            if (a->kind != Type::Function || pt.params.size() != a->params.size() || pt.variadic != a->variadic) return false;
            if (pt.isConst != a->constMethod) return false;
            for (size_t k = 0; k < pt.params.size(); ++k) if (!pt.params[k] || !deduce(pt.params[k], a->params[k], tscope, bindings, ints)) return false;
            a = a->inner;
            continue;
        }
        if (pt.kind == cxx::DeclaratorPart::Array) {
            if (a->kind != Type::Array && a->kind != Type::Pointer) return false;
            // the bound: "T (&a)[N]" deduces N from an array argument (14.8.2.4); a constant bound must match
            cxx::Expr* bound = pt.arraySize;
            bool boundIsParam = false;
            if (bound && bound->kind == cxx::Expr::Name && bound->name.parts.size() == 1 && !bound->name.global && tscope) {
                const std::vector<Symbol*>* v = tscope->find(bound->name.parts[0].ident);
                if (v) for (size_t k = 0; k < v->size(); ++k) if ((*v)[k]->kind == Symbol::TemplateParam) boundIsParam = true;
            }
            if (boundIsParam) {
                if (a->kind != Type::Array || a->arraySize < 0) return false;   // a pointer carries no bound
                const std::string& pn = bound->name.parts[0].ident;
                std::map<std::string, long>::iterator it = ints.find(pn);
                if (it == ints.end()) ints[pn] = a->arraySize;
                else if (it->second != a->arraySize) return false;
            } else if (bound && byRef && a->kind == Type::Array && a->arraySize >= 0) {
                long n;
                if (constantValue(bound, n) && n != a->arraySize) return false;
            }
            a = a->inner;
            continue;
        }
        return false;
    }
    const cxx::DeclSpec& spec = p->spec;
    if (m_strictDeduce && ((spec.isConst && !a->isConst) || (spec.isVolatile && !a->isVolatile))) return false;
    if (spec.isConst && a->isConst) a = m_types.unqualified(a);
    // a qualified name (typename Id<T>::type) is a non-deduced context (14.8.2.4/4): nothing to learn from it, the
    // argument must convert to the substituted type
    if (spec.kind == cxx::DeclSpec::Named && spec.name.parts.size() > 1) return true;
    if (spec.kind == cxx::DeclSpec::Named && spec.name.parts.size() == 1 && !spec.name.global) {
        const cxx::NameComponent& c = spec.name.parts[0];
        // a template parameter?
        bool isParam = false;
        if (tscope) { const std::vector<Symbol*>* v = tscope->find(c.ident); if (v) for (size_t k = 0; k < v->size(); ++k) if ((*v)[k]->kind == Symbol::TemplateParam) isParam = true; }
        if (isParam && !c.hasTemplateArgs && m_fixedTemplateParams.count(c.ident)) return true;   // given explicitly: not deduced, converted
        if (isParam && !c.hasTemplateArgs) {
            std::map<std::string, const Type*>::iterator it = bindings.find(c.ident);
            const Type* bound = m_types.unqualified(a);
            if (byRef && (spec.isConst)) bound = m_types.unqualified(a);
            if (it == bindings.end()) { bindings[c.ident] = bound; return true; }
            return it->second == bound;
        }
        if (c.hasTemplateArgs) {
            // Class<T, N> against an instantiation of the same template
            if (a->kind != Type::Class || !a->decl || !a->decl->instantiatedFrom) return false;
            if (a->decl->instantiatedFrom->name != c.ident) return false;
            if (c.templateArgs.size() != a->decl->templateArgs.size()) return false;
            for (size_t k = 0; k < c.templateArgs.size(); ++k) {
                const cxx::TemplateArg& ta = c.templateArgs[k];
                if (ta.type) { cxx::Param sub; sub.spec = ta.type->spec; sub.decl = ta.type->decl; if (!a->decl->templateArgs[k] || !deduce(&sub, a->decl->templateArgs[k], tscope, bindings, ints)) return false; }
                else if (ta.expr) {
                    // a parameter of this template is deduced; any other argument is a value that must match: a
                    // literal, or a parameter of the enclosing class template bound in its instantiation
                    // ("Matrix<T, R, K>" in a member template of Matrix<T, R, C>), evaluated where the template is
                    const bool single = ta.expr->kind == cxx::Expr::Name && ta.expr->name.parts.size() == 1;
                    const std::string pn = single ? ta.expr->name.parts[0].ident : std::string("");
                    bool np = false;
                    if (single && tscope) { const std::vector<Symbol*>* v = tscope->find(pn); if (v) np = !v->empty(); }
                    if (np) {
                        if (a->decl->templateArgs[k]) { cxx::Param sub; sub.spec.kind = cxx::DeclSpec::Named; sub.spec.name = ta.expr->name; if (!deduce(&sub, a->decl->templateArgs[k], tscope, bindings, ints)) return false; }
                        else if (ints.count(pn) && ints[pn] != a->decl->templateIntArgs[k]) return false;
                        else ints[pn] = a->decl->templateIntArgs[k];
                    } else {
                        if (!ta.expr->semaType && tscope) expr(ta.expr, tscope);
                        long v;
                        if (!constantValue(ta.expr, v) || a->decl->templateArgs[k] || v != a->decl->templateIntArgs[k]) return false;
                    }
                } else return false;
            }
            return true;
        }
    }
    // a concrete type names no parameter: nothing to deduce (the argument must convert to it, which overload
    // resolution checks); in partial ordering it must match
    if (!m_strictDeduce) return true;
    const Type* pt = typeOf(spec, tscope ? tscope : m_prog.global);
    return m_types.unqualified(pt) == m_types.unqualified(a) || pt->kind == Type::Error;
}

Symbol* Sema::instantiateFunctionTemplate(Symbol* tmpl, const std::vector<cxx::Expr*>& args, size_t firstArg, Scope* in, const cxx::Loc& loc, const std::vector<cxx::TemplateArg>* explicitArgs)
{
    (void)loc;
    cxx::Decl* decl = tmpl->declAst;
    if (!decl) return 0;
    cxx::Declarator* dcl = decl->kind == cxx::Decl::Function ? decl->declarator : (decl->declarators.empty() ? 0 : decl->declarators[0].decl);
    if (!dcl || !dcl->isFunction()) return 0;
    const cxx::DeclaratorPart* fp = dcl->functionPart();
    Scope* tscope = tmpl->templateParams.empty() ? 0 : tmpl->templateParams[0]->scope;
    std::map<std::string, const Type*> bind; std::map<std::string, long> ints;
    size_t nargs = args.size() - firstArg;
    if (nargs > fp->params.size() && !fp->variadic) return 0;
    for (size_t k = nargs; k < fp->params.size(); ++k) if (!fp->params[k]->defaultValue) return 0;   // too few arguments: not a candidate, no instantiation
    // the template arguments written on the call bind the first parameters (14.8.1); they are not deduced
    const std::set<std::string> savedFixed = m_fixedTemplateParams;
    m_fixedTemplateParams.clear();
    if (explicitArgs && !explicitArgs->empty()) {
        std::vector<const Type*> etypes; std::vector<long> eints; std::string etext;
        if (explicitArgs->size() > tmpl->templateParams.size() || !templateArgText(*explicitArgs, in, etypes, eints, etext)) { m_fixedTemplateParams = savedFixed; return 0; }
        for (size_t i = 0; i < etypes.size(); ++i) {
            const std::string& pn = tmpl->templateParams[i]->name;
            const bool typeParam = tmpl->templateParams[i]->type && tmpl->templateParams[i]->type->kind == Type::Dependent;
            if (typeParam != (etypes[i] != 0)) { m_fixedTemplateParams = savedFixed; return 0; }   // a type for a value parameter or the other way round
            if (etypes[i]) bind[pn] = etypes[i]; else ints[pn] = eints[i];
            m_fixedTemplateParams.insert(pn);
        }
    }
    for (size_t a = 0; a < nargs && a < fp->params.size(); ++a) {
        if (!args[firstArg + a]->semaType) expr(args[firstArg + a], in);
        const Type* at = args[firstArg + a]->semaType;
        if (!at || at->kind == Type::Error || !deduce(fp->params[a], at, tscope, bind, ints)) { m_fixedTemplateParams = savedFixed; return 0; }
    }
    m_fixedTemplateParams = savedFixed;
    return instantiateBoundFunctionTemplate(tmpl, bind, ints);
}

// the specialisation of the function template tmpl for the parameters bound in bind (types) and ints (values):
// the one made before for them, or a new one, its declaration a copy of the template's in a scope that binds the
// parameters, with the body queued for analysis
Symbol* Sema::instantiateBoundFunctionTemplate(Symbol* tmpl, std::map<std::string, const Type*>& bind, std::map<std::string, long>& ints)
{
    cxx::Decl* decl = tmpl->declAst;
    if (!decl) return 0;
    std::string key = "<";
    for (size_t i = 0; i < tmpl->templateParams.size(); ++i) {
        const std::string& pn = tmpl->templateParams[i]->name;
        if (i) key += ", ";
        if (bind.count(pn)) key += bind[pn]->text();
        else if (ints.count(pn)) key += numText(ints[pn]);
        else return 0;                                   // not every parameter deduced
    }
    key += ">";
    std::map<std::string, Symbol*>::iterator it = tmpl->instantiations.find(key);
    if (it != tmpl->instantiations.end()) return it->second;
    Scope* parent = tmpl->ownerClass ? tmpl->ownerClass->members : (tmpl->scope && tmpl->scope->kind == Scope::TemplateParams ? tmpl->scope->parent : tmpl->scope);
    Scope* iscope = m_prog.newScope(Scope::Instantiation, parent, tmpl);
    for (size_t i = 0; i < tmpl->templateParams.size(); ++i) {
        const std::string& pn = tmpl->templateParams[i]->name;
        Symbol* b = m_prog.newSymbol(bind.count(pn) ? Symbol::Typedef : Symbol::Enumerator, pn, iscope);
        if (bind.count(pn)) b->type = bind[pn]; else { b->type = m_types.basic(Type::Int); b->hasConstValue = true; b->constValue = ints[pn]; }
    }
    cxx::Decl* copy = cxx::cloneDecl(decl, nodeArena());   // annotations belong to this instantiation
    if (copy->kind != cxx::Decl::Function) copy->declarators.resize(1);
    cxx::Declarator* dcopy = copy->kind == cxx::Decl::Function ? copy->declarator : copy->declarators[0].decl;
    // the instantiation is declared in the instantiation scope under the class (or namespace): a member template
    // defined out of class ("Box<int>::conv") drops the qualifier of its definition
    if (dcopy->name.parts.size() > 1) { cxx::NameComponent last = dcopy->name.parts.back(); dcopy->name.parts.clear(); dcopy->name.parts.push_back(last); dcopy->name.global = false; }
    dcopy->name.parts.back().ident = tmpl->name + key;
    dcopy->name.parts.back().hasTemplateArgs = false;
    // substitution: a parameter or result type that does not exist for these arguments (typename T::type with no
    // such member) is no error, the template is just not a candidate (14.8.2/2, SFINAE)
    const size_t diagMark = m_diags.size();
    const std::set<std::string> savedUnknown = m_reportedUnknown;
    const Type* base = typeOf(copy->spec, iscope);
    std::vector<Symbol*> params;
    const Type* ft = applyDeclarator(base, *dcopy, iscope, &params);
    bool substitutionFailed = false;
    if (ft && ft->kind == Type::Function) {
        for (size_t k = 0; k < ft->params.size(); ++k) if (typeHasError(ft->params[k])) substitutionFailed = true;
        if (copy->spec.hasType() && typeHasError(ft->inner)) substitutionFailed = true;   // a constructor template has no result type
    }
    if (substitutionFailed) { m_diags.resize(diagMark); m_reportedUnknown = savedUnknown; return 0; }
    Symbol* fn = declareFunction(copy, dcopy, copy->spec, iscope, cxx::AccessPublic, ft, params);
    if (!fn) return 0;
    fn->instantiatedFrom = tmpl;
    fn->ownerClass = tmpl->ownerClass;
    fn->isDependentContext = false;
    if (tmpl->ownerClass && tmpl->name == tmpl->ownerClass->ctorName()) {
        // a constructor template: declared in the instantiation scope, so declareFunction did not see the class
        fn->isConstructor = true;
        fn->type = m_types.function(m_types.basic(Type::Void), fn->type->params, fn->type->variadic, fn->type->constMethod);
    }
    for (size_t i = 0; i < tmpl->templateParams.size(); ++i) {
        const std::string& pn = tmpl->templateParams[i]->name;
        fn->templateArgs.push_back(bind.count(pn) ? bind[pn] : 0);
        fn->templateIntArgs.push_back(ints.count(pn) ? ints[pn] : 0);
    }
    tmpl->instantiations[key] = fn;
    ++m_stats.functionInstantiations;
    if (copy->kind == cxx::Decl::Function) {
        fn->hasBody = true; fn->definition = copy;
        PendingBody pb; pb.fn = fn; pb.def = copy; pb.scope = iscope; queueBody(pb);
    }
    return fn;
}

// t, or a part of it, is the error type (a substitution that failed)
bool Sema::typeHasError(const Type* t)
{
    if (!t) return false;
    if (t->kind == Type::Error) return true;
    if (t->inner && typeHasError(t->inner)) return true;
    for (size_t i = 0; i < t->params.size(); ++i) if (typeHasError(t->params[i])) return true;
    return false;
}

// partial ordering of function templates (14.5.5.2): a is at least as specialised as b when every parameter of b
// can be deduced from a's parameter types with a's template parameters replaced by unique types (a const in b's
// parameter needs a const in a's)
bool Sema::atLeastAsSpecialized(Symbol* a, Symbol* b)
{
    if (!a || !b || !a->declAst || !b->declAst) return false;
    cxx::Declarator* ad = a->declAst->kind == cxx::Decl::Function ? a->declAst->declarator : (a->declAst->declarators.empty() ? 0 : a->declAst->declarators[0].decl);
    cxx::Declarator* bd = b->declAst->kind == cxx::Decl::Function ? b->declAst->declarator : (b->declAst->declarators.empty() ? 0 : b->declAst->declarators[0].decl);
    if (!ad || !bd || !ad->isFunction() || !bd->isFunction()) return false;
    const cxx::DeclaratorPart* afp = ad->functionPart();
    const cxx::DeclaratorPart* bfp = bd->functionPart();
    if (afp->params.size() != bfp->params.size()) return false;
    Scope* ascope = a->templateParams.empty() ? 0 : a->templateParams[0]->scope;
    Scope* unique = m_prog.newScope(Scope::Instantiation, ascope && ascope->parent ? ascope->parent : m_prog.global, a);
    for (size_t i = 0; i < a->templateParams.size(); ++i) {
        const std::string& pn = a->templateParams[i]->name;
        if (a->templateParams[i]->type && a->templateParams[i]->type->kind == Type::Dependent) {
            Symbol* u = m_prog.newSymbol(Symbol::Class, "$unique" + numText(static_cast<long>(i)), unique);
            u->type = m_types.classType(u); u->isDefinition = true; u->isStruct = true;
            Symbol* td = m_prog.newSymbol(Symbol::Typedef, pn, unique); td->type = u->type;
        } else { Symbol* v = m_prog.newSymbol(Symbol::Enumerator, pn, unique); v->type = m_types.basic(Type::Int); v->hasConstValue = true; v->constValue = 1; }
    }
    Scope* bscope = b->templateParams.empty() ? 0 : b->templateParams[0]->scope;
    std::map<std::string, const Type*> bb; std::map<std::string, long> bi;
    const size_t diagMark = m_diags.size();
    const bool savedStrict = m_strictDeduce;
    m_strictDeduce = true;
    bool ok = true;
    for (size_t k = 0; k < afp->params.size() && ok; ++k) {
        const cxx::Param* ap = afp->params[k];
        const Type* at = typeOf(ap->spec, unique);
        if (ap->decl) at = applyDeclarator(at, *ap->decl, unique, 0);
        if (typeHasError(at) || !bfp->params[k] || !deduce(bfp->params[k], at, bscope, bb, bi)) ok = false;
    }
    m_strictDeduce = savedStrict;
    m_diags.resize(diagMark);
    return ok;
}

Symbol* Sema::classOperator(const std::string& op, cxx::Expr* e, const std::vector<cxx::Expr*>& operands, Scope* in, const Type*& result)
{
    // member candidates on the first operand, then free functions; the first viable set wins
    const Type* lt = m_types.unref(operands[0]->semaType);
    std::string name = "operator" + op;
    if (lt && lt->kind == Type::Class && lt->decl) {
        std::vector<Symbol*> cands;
        lookupMember(lt->decl, name, cands);
        if (!cands.empty()) {
            std::vector<cxx::Expr*> args(operands.begin() + 1, operands.end());
            Symbol* f = resolveOverload(cands, args, 0, operands[0], in, e->loc, lt->decl->name + "::" + name, false);
            if (f) { result = resultOfCall(f); return f; }
        }
    }
    std::vector<Symbol*> cands;
    lookupUnqualified(name, in, false, &cands);
    // argument-dependent lookup: the namespaces associated with the operands' types (those of the classes, their
    // bases and template arguments; in-class friend definitions among the functions found)
    adlCandidates(name, operands, 0, cands);
    std::vector<Symbol*> free;
    for (size_t i = 0; i < cands.size(); ++i) if ((cands[i]->kind == Symbol::Function && !cands[i]->ownerClass) || cands[i]->kind == Symbol::FunctionTemplate) free.push_back(cands[i]);
    if (free.empty()) return 0;
    Symbol* f = resolveOverload(free, operands, 0, 0, in, e->loc, name, false);
    if (f) { result = resultOfCall(f); return f; }
    return 0;
}

const Type* Sema::exprName(cxx::Expr* e, Scope* in)
{
    std::vector<Symbol*> all;
    Symbol* s = resolveName(e->name, in, false, &all);
    if (!s) {
        ++m_stats.namesUnresolved;
        std::string text = e->name.text();
        if (!m_reportedUnknown.count(text)) { m_reportedUnknown.insert(text); error(e->loc, "S101", "undeclared identifier '" + text + "'"); }
        return m_types.error();
    }
    ++m_stats.namesResolved;
    e->semaSym = s;
    if (s->kind == Symbol::Variable || s->kind == Symbol::Enumerator) accessCheck(s, e->loc, in);
    switch (s->kind) {
    case Symbol::Variable:
        e->semaLvalue = true;
        s->isUsed = true;
        if (s->hasConstValue) { e->semaConst = true; e->semaValue = s->constValue; }
        return s->type;
    case Symbol::Enumerator:
        e->semaConst = true; e->semaValue = s->constValue;
        return s->type;
    case Symbol::Function: case Symbol::FunctionTemplate:
        // an overload set: the call (or the address-of) chooses; until then the first declaration's type
        e->semaLvalue = true;
        return s->type ? s->type : m_types.error();
    case Symbol::TemplateParam:
        if (s->type && s->type->kind == Type::Dependent) return s->type;
        e->semaConst = s->hasConstValue; e->semaValue = s->constValue;
        return s->type ? s->type : m_types.basic(Type::Int);
    case Symbol::Class: case Symbol::Typedef: case Symbol::Enum: case Symbol::ClassTemplate:
        // a type used where a value is expected: the caller (call / cast) handles it; here it is an error
        return s->kind == Symbol::Typedef ? s->type : s->kind == Symbol::Class ? m_types.classType(s) : s->kind == Symbol::Enum ? m_types.enumType(s) : m_types.error();
    default: return m_types.error();
    }
}

const Type* Sema::exprMember(cxx::Expr* e, Scope* in)
{
    const Type* ot = expr(e->kids[0], in);
    const Type* t = m_types.unref(ot);
    if (e->arrowMember) {
        // operator->: the object becomes the operand of a unary node that calls it and yields the pointer; applied
        // again while the result is a class with operator-> (13.5.6: until a pointer comes out)
        for (int depth = 0; depth < 32 && t->kind == Type::Class && t->decl; ++depth) {
            std::vector<Symbol*> ops; lookupMember(t->decl, "operator->", ops);
            if (ops.empty()) break;
            cxx::Expr* w = nodeArena().expr(cxx::Expr::Unary, e->kids[0]->loc);
            w->text = "->";
            w->kids.push_back(e->kids[0]);
            w->semaSym = ops[0];
            w->semaType = resultOfCall(ops[0]);
            noteUse(ops[0]);
            e->kids[0] = w;
            t = m_types.unref(w->semaType);
        }
        if (t->kind != Type::Pointer) { if (t->kind != Type::Error && t->kind != Type::Dependent) error(e->loc, "S117", "'->' applied to a non-pointer of type " + t->text()); return t->kind == Type::Dependent ? t : m_types.error(); }
        t = t->inner;
    }
    if (t->kind == Type::Dependent || t->kind == Type::Error) return t;
    const std::string& name = e->name.parts.back().ident;
    bool destructorName = !name.empty() && name[0] == '~';
    if (t->kind != Type::Class || !t->decl) {
        // p->~T() on a scalar: a pseudo-destructor call, nothing to resolve
        if (destructorName) return m_types.function(m_types.basic(Type::Void), std::vector<const Type*>(), false, false);
        error(e->loc, "S112", "member access on a non-class type " + t->text());
        return m_types.error();
    }
    Symbol* cls = t->decl;
    if (!cls->isDefinition) { error(e->loc, "S113", "incomplete class '" + cls->qualifiedName() + "'"); return m_types.error(); }
    std::vector<Symbol*> out;
    if (destructorName) lookupMember(cls, "~" + cls->ctorName(), out);   // however the type was spelled (a typedef, a template parameter)
    else if (e->name.parts.size() > 1) { Scope* q = scopeForQualifier(e->name, cls->members); if (q && q->kind == Scope::Class) lookupMember(q->owner, name, out); }
    else lookupMember(cls, name, out);
    if (out.empty()) {
        ++m_stats.namesUnresolved;
        std::string key = cls->qualifiedName() + "::" + name;
        if (!m_reportedUnknown.count(key)) { m_reportedUnknown.insert(key); error(e->loc, "S102", "'" + cls->qualifiedName() + "' has no member '" + name + "'"); }
        return m_types.error();
    }
    ++m_stats.namesResolved;
    Symbol* s = out[0];
    e->semaSym = s;
    if (!destructorName && (s->kind == Symbol::Variable || s->kind == Symbol::Function || s->kind == Symbol::FunctionTemplate)) {
        // a non-static member of a base held more than once, or names from unrelated bases (cl C2385); the
        // class that names the member is the qualifier's ("d.B::a") or the object's
        Symbol* naming = cls;
        if (e->name.parts.size() > 1) { Scope* q = scopeForQualifier(e->name, cls->members); if (q && q->kind == Scope::Class && q->owner) naming = q->owner; }
        bool ambiguous = false;
        for (size_t k = 1; k < out.size() && !ambiguous; ++k)
            if (out[k]->ownerClass && s->ownerClass && out[k]->ownerClass != s->ownerClass && !derivesFrom(out[k]->ownerClass, s->ownerClass, 0) && !derivesFrom(s->ownerClass, out[k]->ownerClass, 0)) ambiguous = true;
        bool nonStatic = false;
        for (size_t k = 0; k < out.size(); ++k) if (!out[k]->isStatic && out[k]->kind != Symbol::Enumerator) nonStatic = true;
        if (!ambiguous && nonStatic && s->ownerClass && naming != s->ownerClass && baseSubobjects(naming, s->ownerClass) > 1) ambiguous = true;
        // the object's own class must hold the naming class once ("d.B::a" with two B in d)
        if (!ambiguous && nonStatic && naming != cls && baseSubobjects(cls, naming) > 1) ambiguous = true;
        if (ambiguous) { error(e->loc, "S144", "ambiguous access of '" + name + "' in '" + cls->qualifiedName() + "' (cl C2385)"); return m_types.error(); }
    }
    if (s->kind == Symbol::Variable || s->kind == Symbol::Enumerator) accessCheck(s, e->loc, in);
    if (s->kind == Symbol::Variable) {
        e->semaLvalue = true;
        if (s->hasConstValue) { e->semaConst = true; e->semaValue = s->constValue; }
        const Type* ft = s->type;
        if (!s->isStatic && !s->isMutable && ft->kind != Type::Reference) ft = m_types.qualified(ft, t->isConst, t->isVolatile);
        return ft;
    }
    if (s->kind == Symbol::Enumerator) { e->semaConst = true; e->semaValue = s->constValue; return s->type; }
    if (s->kind == Symbol::Function || s->kind == Symbol::FunctionTemplate) { e->semaLvalue = true; return s->type ? s->type : m_types.error(); }
    return s->kind == Symbol::Typedef ? s->type : m_types.error();
}

const Type* Sema::exprCall(cxx::Expr* e, Scope* in)
{
    // "(f)(x)", "(e.scale)(x)": the parentheses do not change what is called
    while (e->kids[0]->kind == cxx::Expr::Paren && e->kids[0]->kids.size() == 1) e->kids[0] = e->kids[0]->kids[0];
    cxx::Expr* callee = e->kids[0];
    std::vector<Symbol*> cands;
    cxx::Expr* thisExpr = 0;
    std::string what;
    if (callee->kind == cxx::Expr::Name) {
        Symbol* s = resolveName(callee->name, in, false, &cands);
        if (s && s->kind == Symbol::Variable) s->isUsed = true;
        what = callee->name.text();
        // argument-dependent lookup (3.4.2): the functions of the namespaces associated with the arguments' types
        // join the candidates, unless ordinary lookup found a class member, a block-scope declaration or something
        // that is not a function (then only those count)
        bool adl = callee->name.parts.size() == 1 && !callee->name.global;
        for (size_t k = 0; k < cands.size() && adl; ++k) {
            Symbol* c = cands[k];
            if ((c->kind != Symbol::Function && c->kind != Symbol::FunctionTemplate) || c->ownerClass || c->isLocal || !c->scope || !c->scope->isNamespaceLike()) adl = false;
        }
        if (adl) {
            for (size_t a = 1; a < e->kids.size(); ++a) if (!e->kids[a]->semaType) expr(e->kids[a], in);
            adlCandidates(callee->name.last(), e->kids, 1, cands);
            if (!s && !cands.empty()) s = cands[0];
        }
        if (!s) {
            for (size_t a = 1; a < e->kids.size(); ++a) if (!e->kids[a]->semaType) expr(e->kids[a], in);
            ++m_stats.namesUnresolved;
            if (!m_reportedUnknown.count(what)) { m_reportedUnknown.insert(what); error(e->loc, "S103", "call to undeclared function '" + what + "'"); }
            return m_types.error();
        }
        if (s->kind == Symbol::Class) {
            // a function with the same name as a class hides the class in a call ("stat(path, &st)")
            std::vector<Symbol*> fns;
            for (size_t k = 0; k < cands.size(); ++k) if (cands[k]->kind == Symbol::Function || cands[k]->kind == Symbol::FunctionTemplate) fns.push_back(cands[k]);
            if (!fns.empty()) { s = fns[0]; cands = fns; }
        }
        if (s->kind == Symbol::Class || s->kind == Symbol::Typedef || s->kind == Symbol::ClassTemplate) {
            // T(args): a functional cast / constructor call the parser could not tell from a call ("M::value_type(k, v)"
            // with M a typedef of a class): the node becomes the functional cast, its operands the arguments
            const Type* t = s->kind == Symbol::Typedef ? s->type : s->kind == Symbol::Class ? m_types.classType(s) : namedType(callee->name, in, "", e->loc);
            e->kids.erase(e->kids.begin());
            e->kind = cxx::Expr::FunctionalCast;
            e->type = 0;
            return exprCast(e, in, t, true);
        }
        if (s->kind == Symbol::Variable) {
            // a function pointer, or a class with operator()
            const Type* vt = m_types.unref(s->type);
            callee->semaSym = s; callee->semaType = s->type;
            if (vt->kind == Type::Pointer && vt->inner->kind == Type::Function) { for (size_t a = 1; a < e->kids.size(); ++a) if (!e->kids[a]->semaType) expr(e->kids[a], in); promoteVariadic(e->kids, 1, vt->inner); return vt->inner->inner; }
            if (vt->kind == Type::Class && vt->decl) { cands.clear(); lookupMember(vt->decl, "operator()", cands); thisExpr = callee; what = vt->decl->name + "::operator()"; }
            else { for (size_t a = 1; a < e->kids.size(); ++a) if (!e->kids[a]->semaType) expr(e->kids[a], in); if (vt->kind != Type::Dependent && vt->kind != Type::Error) error(e->loc, "S118", "'" + what + "' is not a function"); return m_types.error(); }
        }
        // a member function called from within a member function: the implicit this
        if (!thisExpr && !cands.empty() && cands[0]->ownerClass && m_currentClass && cands[0]->kind == Symbol::Function && !cands[0]->isStatic) thisExpr = callee;
        callee->semaType = cands.empty() ? m_types.error() : cands[0]->type;
    } else if (callee->kind == cxx::Expr::Member) {
        const Type* mt = exprMember(callee, in);
        Symbol* s = callee->semaSym;
        if (!s) {
            for (size_t a = 1; a < e->kids.size(); ++a) if (!e->kids[a]->semaType) expr(e->kids[a], in);
            if (mt && mt->kind == Type::Function && !callee->name.parts.empty() && !callee->name.last().empty() && callee->name.last()[0] == '~') return m_types.basic(Type::Void);   // pseudo-destructor
            return m_types.error();
        }
        what = (callee->kids[0]->semaType ? m_types.unref(callee->kids[0]->semaType)->text() : std::string("?")) + "::" + callee->name.parts.back().ident;
        if (s->kind == Symbol::Function || s->kind == Symbol::FunctionTemplate) {
            const Type* ot = m_types.unref(callee->kids[0]->semaType);
            if (callee->arrowMember && ot && ot->kind == Type::Pointer) ot = ot->inner;
            if (ot && ot->kind == Type::Class && ot->decl) { if (callee->name.parts.size() > 1) { Scope* q = scopeForQualifier(callee->name, ot->decl->members); if (q && q->kind == Scope::Class) lookupMember(q->owner, s->name, cands); } else lookupMember(ot->decl, s->name, cands); }
            thisExpr = callee->kids[0];
        } else {
            // a data member: a function pointer (called through its value) or a class with operator()
            callee->semaType = mt;
            const Type* vt = m_types.unref(mt);
            if (vt->kind == Type::Pointer && vt->inner->kind == Type::Function) { for (size_t a = 1; a < e->kids.size(); ++a) if (!e->kids[a]->semaType) expr(e->kids[a], in); promoteVariadic(e->kids, 1, vt->inner); return vt->inner->inner; }
            if (vt->kind == Type::Class && vt->decl) { lookupMember(vt->decl, "operator()", cands); thisExpr = callee; }
            else { for (size_t a = 1; a < e->kids.size(); ++a) if (!e->kids[a]->semaType) expr(e->kids[a], in); return vt->kind == Type::Dependent ? vt : m_types.error(); }
        }
    } else {
        const Type* ct = m_types.unref(expr(callee, in));
        for (size_t a = 1; a < e->kids.size(); ++a) if (!e->kids[a]->semaType) expr(e->kids[a], in);
        if (ct->kind == Type::Pointer && ct->inner->kind == Type::Function) { promoteVariadic(e->kids, 1, ct->inner); return ct->inner->inner; }
        if (ct->kind == Type::Function) { promoteVariadic(e->kids, 1, ct); return ct->inner; }
        if (ct->kind == Type::Class && ct->decl) { lookupMember(ct->decl, "operator()", cands); thisExpr = callee; what = "operator()"; }
        else { if (ct->kind != Type::Dependent && ct->kind != Type::Error) error(e->loc, "S118", "called object is not a function"); return ct->kind == Type::Dependent ? ct : m_types.error(); }
    }
    for (size_t a = 1; a < e->kids.size(); ++a) if (!e->kids[a]->semaType) expr(e->kids[a], in);
    const std::vector<cxx::TemplateArg>* explicitArgs = (callee->kind == cxx::Expr::Name || callee->kind == cxx::Expr::Member) && !callee->name.parts.empty() && callee->name.parts.back().hasTemplateArgs ? &callee->name.parts.back().templateArgs : 0;
    Symbol* f = resolveOverload(cands, e->kids, 1, thisExpr, in, e->loc, what, true, explicitArgs);
    if (!f) return m_types.error();
    e->semaSym = f;
    if (f->ownerClass) accessCheck(f, e->loc, in);
    const Type* r = resultOfCall(f);
    if (r->kind == Type::Reference) e->semaLvalue = true;
    return r;
}

// a class of namespace std by name (type_info, bad_cast), 0 when it is not declared (the header is not included)
Symbol* Sema::stdClass(const char* name)
{
    const std::vector<Symbol*>* ns = m_prog.global->find("std");
    if (!ns) return 0;
    for (size_t i = 0; i < ns->size(); ++i) {
        Symbol* n = (*ns)[i];
        if (n->kind != Symbol::Namespace || !n->members) continue;
        const std::vector<Symbol*>* v = n->members->find(name);
        if (v) for (size_t k = 0; k < v->size(); ++k) if ((*v)[k]->kind == Symbol::Class && (*v)[k]->isDefinition) return (*v)[k];
    }
    return 0;
}

// 5.2.7: dynamic_cast<T>(v) converts to a pointer or reference to a class, or to void*; a conversion to a base is
// static; anything else (a cast down or across, to void*) needs a polymorphic operand and, for a reference, the
// std::bad_cast it throws
bool Sema::dynamicCastCheck(cxx::Expr* e, const Type* to)
{
    const Type* vt = valueType(e->kids[0]);
    if (vt->kind == Type::Dependent || vt->kind == Type::Error) return true;
    const Type* srcClass = 0;
    const Type* target = 0;
    if (to->kind == Type::Pointer) {
        target = m_types.unqualified(to->inner);
        if (vt->kind != Type::Pointer) { error(e->loc, "S147", "dynamic_cast to a pointer needs a pointer operand, not " + vt->text()); return false; }
        srcClass = m_types.unqualified(vt->inner);
    } else if (to->kind == Type::Reference) {
        target = m_types.unqualified(to->inner);
        srcClass = m_types.unqualified(m_types.unref(e->kids[0]->semaType));
    } else { error(e->loc, "S147", "dynamic_cast converts to a pointer or reference to a class, or to void*, not to " + to->text()); return false; }
    if (!srcClass || srcClass->kind != Type::Class || !srcClass->decl) { error(e->loc, "S147", "the operand of dynamic_cast must be a class, not " + srcClass->text()); return false; }
    if (!target || (target->kind != Type::Class && !(to->kind == Type::Pointer && target->kind == Type::Void))) { error(e->loc, "S147", "dynamic_cast converts to a pointer or reference to a class, or to void*, not to " + to->text()); return false; }
    if (target->kind == Type::Class && target->decl && (target->decl == srcClass->decl || derivesFrom(srcClass->decl, target->decl, 0))) return true;   // static
    if (!srcClass->decl->isPolymorphic) { error(e->loc, "S147", "'" + srcClass->decl->qualifiedName() + "' is not a polymorphic type: dynamic_cast needs a class with a virtual function (cl C2683)"); return false; }
    if (target->kind == Type::Class && target->decl && !target->decl->isDefinition) { error(e->loc, "S113", "incomplete class '" + target->decl->qualifiedName() + "' in a dynamic_cast"); return false; }
    if (to->kind == Type::Reference && !stdClass("bad_cast")) { error(e->loc, "S146", "dynamic_cast to a reference throws std::bad_cast: include <typeinfo>"); return false; }
    return true;
}

const Type* Sema::exprCast(cxx::Expr* e, Scope* in, const Type* to, bool checked)
{
    // e->kids are the operands (one for casts, any number for a functional cast)
    for (size_t k = 0; k < e->kids.size(); ++k) expr(e->kids[k], in);
    if (!to || to->kind == Type::Error) return m_types.error();
    if (to->kind == Type::Dependent) return to;
    const Type* tu = m_types.unref(to);
    if (e->kind == cxx::Expr::NamedCast && e->text == "dynamic_cast" && e->kids.size() == 1 && e->kids[0]->semaType && !dynamicCastCheck(e, to)) return m_types.error();
    if (e->kids.size() == 1 && e->kids[0]->semaType) {
        // a cast between a class and one of its bases held more than once (up or down; cl C2594)
        const Type* fu = m_types.unqualified(m_types.unref(e->kids[0]->semaType));
        const Type* toc = m_types.unqualified(tu);
        if (fu->kind == Type::Pointer && toc->kind == Type::Pointer) { fu = m_types.unqualified(fu->inner); toc = m_types.unqualified(toc->inner); }
        else if (to->kind != Type::Reference && toc->kind != Type::Class) fu = 0;
        if (fu && toc && fu->kind == Type::Class && toc->kind == Type::Class && fu->decl && toc->decl && fu != toc) {
            Symbol* d = derivesFrom(fu->decl, toc->decl, 0) ? fu->decl : derivesFrom(toc->decl, fu->decl, 0) ? toc->decl : 0;
            Symbol* b = d == fu->decl ? toc->decl : fu->decl;
            if (d && baseSubobjects(d, b) > 1) {
                error(e->loc, "S143", "ambiguous conversions from '" + e->kids[0]->semaType->text() + "' to '" + to->text() + "': '" + d->qualifiedName() + "' has more than one '" + b->qualifiedName() + "' (cl C2594)");
                return m_types.error();
            }
        }
    }
    // a C-style cast and static_cast convert as an initialisation does (5.4/5, 5.2.9/2): a class operand goes
    // through its conversion function, a class target through a constructor; reinterpret_cast and const_cast
    // do not (the reference forms keep their address semantics below)
    const bool explicitConv = checked || (e->kids.size() == 1 && (e->kind == cxx::Expr::Cast || (e->kind == cxx::Expr::NamedCast && e->text == "static_cast")));
    if (tu->kind == Type::Class && tu->decl && (checked || (explicitConv && to->kind != Type::Reference))) {
        if (!tu->decl->isDefinition) { error(e->loc, "S113", "incomplete class '" + tu->decl->qualifiedName() + "' in a cast"); return to; }
        std::vector<Symbol*> ctors;
        lookupMember(tu->decl, tu->decl->ctorName(), ctors);
        if (e->kids.size() == 1) {
            // a single operand may also be a conversion operator of its class
            Conversion c = convert(e->kids[0], tu, in);
            if (c.rank == Conversion::UserDefined && c.userFunction && !c.userFunction->isConstructor) { e->semaSym = c.userFunction; return to; }
        }
        Symbol* f = resolveOverload(ctors, e->kids, 0, 0, in, e->loc, tu->decl->name + "::" + tu->decl->name, true);
        accessCheck(f, e->loc, in);
        if (f) e->semaSym = f;
        return to;
    }
    if (e->kids.size() == 1 && explicitConv) {
        Conversion c = convert(e->kids[0], tu, in);
        if (checked && c.rank == Conversion::None && tu->isScalar() && !valueType(e->kids[0])->isScalar() && valueType(e->kids[0])->kind != Type::Class)
            error(e->loc, "S105", "cannot convert " + valueType(e->kids[0])->text() + " to " + tu->text());
        if (c.userFunction && c.userFunction->isConversion) e->semaSym = c.userFunction;
        if (checked && e->kids[0]->semaConst && tu->isIntegral() && !widensUnsigned(e->kids[0]->semaType, tu, e->kids[0]->semaValue)) { e->semaConst = true; e->semaValue = fitTo(e->kids[0]->semaValue, tu); }
    }
    if (e->kids.size() == 1 && e->kids[0]->semaConst && tu->isIntegral() && !checked && !widensUnsigned(e->kids[0]->semaType, tu, e->kids[0]->semaValue)) { e->semaConst = true; e->semaValue = fitTo(e->kids[0]->semaValue, tu); }
    // a floating constant cast to an integral type is an integral constant expression (5.19), folded as cl folds
    // it: modulo 2^N ((int)3e9 is B2D05E00h); a 64-bit value beyond semaValue's 32 bits is left to IrGen
    const Type* iu = m_types.unqualified(tu);
    if (e->kids.size() == 1 && !e->semaConst && !e->semaSym && iu->isIntegral() && e->kids[0]->semaType && m_types.unref(e->kids[0]->semaType)->isFloating()) {
        double d = 0, raw = 0;
        unsigned long long bits = 0;
        if (floatingConstant(e->kids[0], d, false, 0)) {
            if (iu->kind == Type::Bool) { e->semaConst = true; e->semaValue = d != 0; }
            else if (foldFloatToInteger(d, m_types.sizeOf(iu) * 8, iu->isUnsigned(), bits, raw)) {
                const long low = static_cast<long>(static_cast<int>(static_cast<unsigned int>(bits & 0xFFFFFFFFULL)));
                if (m_types.sizeOf(iu) < 8 || static_cast<long long>(bits) == static_cast<long long>(low)) { e->semaConst = true; e->semaValue = low; }
            }
        }
    }
    return to;
}

// the value of an arithmetic constant expression of a floating type, or of an integer constant on its way to one,
// as cl folds it (IrGen::foldArith gives the same values): C++98 takes a floating literal cast to an integral type
// as an integral constant expression; cl also "(int)(2.0 * 1.5)", "(int)-2.5" and a const double initialised by
// a constant. toFloating: e is converted to a floating type next, where a folded cast of a floating constant to an
// integer has its untruncated value ((unsigned char)300.0 + 0.5 is 300.5, as cl)
bool Sema::floatingConstant(cxx::Expr* e, double& v, bool toFloating, int depth)
{
    if (!e || depth > 64 || !e->semaType) return false;
    const Type* t = m_types.unqualified(m_types.unref(e->semaType));
    if (!t->isArithmetic()) return false;
    cxx::Expr* x = e;
    while (x->kids.size() == 1 && !x->semaSym && (x->kind == cxx::Expr::Paren || (toFloating && x->kind == cxx::Expr::Unary && x->text == "+" && x->kids[0]->semaType
                                                   && m_types.unqualified(m_types.unref(x->semaType)) == m_types.unqualified(m_types.unref(x->kids[0]->semaType)))))
        x = x->kids[0];
    const bool cast = (x->kind == cxx::Expr::Cast || (x->kind == cxx::Expr::NamedCast && x->text == "static_cast") || x->kind == cxx::Expr::FunctionalCast) && x->kids.size() == 1 && !x->semaSym;
    const Type* xt = x->semaType ? m_types.unqualified(m_types.unref(x->semaType)) : t;
    if (cast && xt->isIntegral() && xt->kind != Type::Bool && x->kids[0]->semaType && m_types.unref(x->kids[0]->semaType)->isFloating()) {
        double d = 0, raw = 0;
        unsigned long long bits = 0;
        if (!floatingConstant(x->kids[0], d, false, depth + 1) || !foldFloatToInteger(d, m_types.sizeOf(xt) * 8, xt->isUnsigned(), bits, raw)) return false;
        v = toFloating ? raw : xt->isUnsigned() ? static_cast<double>(bits) : static_cast<double>(static_cast<long long>(bits));
        return true;
    }
    if (t->isIntegral() && e->semaConst) {
        v = static_cast<double>(e->semaValue);
        if (t->isUnsigned() && m_types.sizeOf(t) <= 4 && v < 0) v += 4294967296.0;
        return true;
    }
    switch (e->kind) {
    case cxx::Expr::FloatLit: v = floatingLiteralValue(e->text, t->kind == Type::Float); break;
    case cxx::Expr::Paren:
        if (e->kids.size() != 1 || !floatingConstant(e->kids[0], v, false, depth + 1)) return false;
        break;
    case cxx::Expr::Unary:
        if (e->kids.size() != 1 || e->semaSym || (e->text != "-" && e->text != "+") || !floatingConstant(e->kids[0], v, false, depth + 1)) return false;
        if (e->text == "-") v = -v;
        break;
    case cxx::Expr::Binary: {
        double a = 0, b = 0;
        if (!t->isFloating() || e->kids.size() != 2 || e->semaSym || !floatingConstant(e->kids[0], a, true, depth + 1) || !floatingConstant(e->kids[1], b, true, depth + 1)) return false;
        // the usual arithmetic conversions: double if either operand is, else float
        const Type* ta = m_types.unref(e->kids[0]->semaType);
        const Type* tb = m_types.unref(e->kids[1]->semaType);
        const bool single = ta->kind != Type::Double && ta->kind != Type::LongDouble && tb->kind != Type::Double && tb->kind != Type::LongDouble;
        if (single) { a = static_cast<float>(a); b = static_cast<float>(b); }
        if (e->text == "+") v = a + b;
        else if (e->text == "-") v = a - b;
        else if (e->text == "*") v = a * b;
        else if (e->text == "/" && b != 0) v = a / b;
        else return false;
        if (single) v = static_cast<float>(v);
        break;
    }
    case cxx::Expr::Cast: case cxx::Expr::NamedCast: case cxx::Expr::FunctionalCast:
        if (e->kids.size() != 1 || e->semaSym || (e->kind == cxx::Expr::NamedCast && e->text != "static_cast")) return false;
        if (!floatingConstant(e->kids[0], v, t->isFloating(), depth + 1)) return false;
        break;
    case cxx::Expr::Name: {
        Symbol* s = e->semaSym;
        if (!t->isFloating() || !s || s->kind != Symbol::Variable || s->isParam || !s->init || !s->type || !s->type->isConst || s->type->isVolatile) return false;
        cxx::Expr* init = s->init;
        if (!init->semaType) {
            // a namespace-scope initializer is analysed after the declarations: a literal is read as it stands
            bool negative = false;
            while (init->kids.size() == 1 && (init->kind == cxx::Expr::Paren || (init->kind == cxx::Expr::Unary && (init->text == "-" || init->text == "+")))) {
                if (init->kind == cxx::Expr::Unary && init->text == "-") negative = !negative;
                init = init->kids[0];
            }
            if (init->kind != cxx::Expr::FloatLit || init->text.empty()) return false;
            const char last = init->text[init->text.size() - 1];
            v = floatingLiteralValue(init->text, last == 'f' || last == 'F');
            if (negative) v = -v;
        } else if (!floatingConstant(init, v, true, depth + 1)) return false;
        break;
    }
    default:
        return false;
    }
    if (v != v) return false;
    if (t->kind == Type::Float) { v = static_cast<float>(v); return true; }
    if (t->isFloating()) return true;
    if (t->kind == Type::Bool) { v = v != 0 ? 1 : 0; return true; }
    if (t->kind == Type::Enum) return false;
    double raw = 0;
    unsigned long long bits = 0;
    if (!foldFloatToInteger(v, m_types.sizeOf(t) * 8, t->isUnsigned(), bits, raw)) return false;
    v = t->isUnsigned() ? static_cast<double>(bits) : static_cast<double>(static_cast<long long>(bits));
    return true;
}

const Type* Sema::exprNew(cxx::Expr* e, Scope* in)
{
    for (size_t p = 0; p < e->placement.size(); ++p) expr(e->placement[p], in);
    const Type* t = typeOf(e->type->spec, in);
    const Type* full = t;
    bool newArray = false;
    if (e->type->decl) {
        cxx::Declarator d = *e->type->decl;
        if (!d.parts.empty() && d.parts.back().kind == cxx::DeclaratorPart::Array) {
            // new T[n]: the bound is a run-time value and the result points at an element
            newArray = true;
            if (d.parts.back().arraySize) expr(d.parts.back().arraySize, in);
            d.parts.pop_back();
        }
        full = applyDeclarator(t, d, in, 0);
    }
    for (size_t k = 0; k < e->kids.size(); ++k) expr(e->kids[k], in);
    abstractCheck(full, e->loc);
    // a placement new whose arguments are not one pointer (new (std::nothrow) T, new (arena, 4) T) calls the global
    // operator new that takes (size_t, the arguments...): the call replaces the arguments and yields the address
    if (!e->placement.empty() && !(e->placement.size() == 1 && (valueType(e->placement[0])->kind == Type::Pointer || valueType(e->placement[0])->kind == Type::NullPtr))) {
        bool anyError = false;
        for (size_t p = 0; p < e->placement.size(); ++p) if (!e->placement[p]->semaType || e->placement[p]->semaType->kind == Type::Error || e->placement[p]->semaType->kind == Type::Dependent) anyError = true;
        if (anyError) return m_types.pointer(full);
        if (newArray || full->kind == Type::Array) { error(e->loc, "S132", "a placement new of an array with arguments other than an address is not supported yet"); return m_types.pointer(full); }
        const Type* cu0 = m_types.unqualified(full);
        if (cu0->kind == Type::Class && cu0->decl && !cu0->decl->isDefinition) { error(e->loc, "S113", "new of incomplete class '" + cu0->decl->qualifiedName() + "'"); return m_types.pointer(full); }
        // the class's own operator new (its own or inherited) hides the global ones (12.5/2), unless ::new
        std::vector<Symbol*> cands;
        if (!e->globalScope && cu0->kind == Type::Class && cu0->decl) lookupMember(cu0->decl, "operator new", cands);
        if (cands.empty()) {
            const std::vector<Symbol*>* v = m_prog.global->find("operator new");
            if (v) for (size_t k = 0; k < v->size(); ++k) if ((*v)[k]->kind == Symbol::Function && !(*v)[k]->ownerClass) cands.push_back((*v)[k]);
        }
        cxx::Expr* sz = nodeArena().expr(cxx::Expr::IntLit, e->loc);
        sz->text = intToString(m_types.sizeOf(cu0) > 0 ? m_types.sizeOf(cu0) : 1);
        sz->semaType = m_types.sizeType(); sz->semaConst = true; sz->semaValue = m_types.sizeOf(cu0) > 0 ? m_types.sizeOf(cu0) : 1;
        std::vector<cxx::Expr*> args;
        args.push_back(sz);
        for (size_t p = 0; p < e->placement.size(); ++p) args.push_back(e->placement[p]);
        Symbol* f = resolveOverload(cands, args, 0, 0, in, e->loc, "operator new", true);
        if (!f) return m_types.pointer(full);
        cxx::Expr* callee = nodeArena().expr(cxx::Expr::Name, e->loc);
        cxx::NameComponent nc; nc.ident = "operator new"; callee->name.parts.push_back(nc);
        callee->semaSym = f; callee->semaType = f->type;
        cxx::Expr* c = nodeArena().expr(cxx::Expr::Call, e->loc);
        c->kids.push_back(callee);
        for (size_t a = 0; a < args.size(); ++a) c->kids.push_back(args[a]);
        c->semaSym = f; c->semaType = m_types.pointer(m_types.basic(Type::Void));
        noteUse(f);
        e->placement.assign(1, c);
    }
    if (newArray) { e->semaSym = 0; return m_types.pointer(full); }
    if (full->kind == Type::Array) { e->semaSym = 0; return m_types.pointer(full->inner); }
    const Type* cu = m_types.unqualified(full);
    if (cu->kind == Type::Class && cu->decl) {
        if (!cu->decl->isDefinition) { error(e->loc, "S113", "new of incomplete class '" + cu->decl->qualifiedName() + "'"); return m_types.pointer(full); }
        std::vector<Symbol*> ctors;
        lookupMember(cu->decl, cu->decl->ctorName(), ctors);
        Symbol* f = resolveOverload(ctors, e->kids, 0, 0, in, e->loc, cu->decl->name + "::" + cu->decl->name, true);
        accessCheck(f, e->loc, in);
        if (f) e->semaSym = f;
    } else if (e->kids.size() == 1) {
        Conversion c = convert(e->kids[0], cu, in);
        if (c.rank == Conversion::None) error(e->loc, "S105", "cannot initialise " + cu->text() + " from " + valueType(e->kids[0])->text());
    }
    return m_types.pointer(full);
}

const Type* Sema::exprUnary(cxx::Expr* e, Scope* in)
{
    const std::string& op = e->text;
    const Type* ot = expr(e->kids[0], in);
    const Type* t = m_types.unref(ot);
    if (t->kind == Type::Dependent) return t;
    if (t->kind == Type::Error) return t;
    if (t->kind == Type::Class && op != "&") {
        const Type* r = 0;
        std::vector<cxx::Expr*> ops; ops.push_back(e->kids[0]);
        Symbol* f = classOperator(op, e, ops, in, r);
        if (f) { e->semaSym = f; if (r->kind == Type::Reference) e->semaLvalue = true; return r; }
        error(e->loc, "S109", "no operator" + op + " for " + t->text());
        return m_types.error();
    }
    if (op == "*") {
        const Type* v = valueType(e->kids[0]);
        if (v->kind != Type::Pointer) { error(e->loc, "S119", "dereference of a non-pointer of type " + v->text()); return m_types.error(); }
        e->semaLvalue = true;
        return v->inner;
    }
    if (op == "&") {
        Symbol* s = e->kids[0]->semaSym;
        if (s && s->kind == Symbol::Function && s->ownerClass && !s->isStatic && e->kids[0]->kind == cxx::Expr::Name && e->kids[0]->name.parts.size() > 1)
            return m_types.memberPointer(m_types.classType(s->ownerClass), s->type);
        // the address of a function: only when the operand names it ("&f", "&C::f"), not when it calls one ("&v[i]")
        if (s && (s->kind == Symbol::Function || s->kind == Symbol::FunctionTemplate) && (e->kids[0]->kind == cxx::Expr::Name || e->kids[0]->kind == cxx::Expr::Member)) return m_types.pointer(s->type);
        if (!e->kids[0]->semaLvalue && ot->kind != Type::Function) { error(e->loc, "S108", "address of a non-lvalue"); return m_types.error(); }
        if (e->kids[0]->semaSym && e->kids[0]->semaSym->kind == Symbol::Variable && e->kids[0]->kind == cxx::Expr::Name && e->kids[0]->name.parts.size() > 1 && e->kids[0]->semaSym->isMember && !e->kids[0]->semaSym->isStatic)
            return m_types.memberPointer(m_types.classType(e->kids[0]->semaSym->ownerClass), e->kids[0]->semaSym->type);
        return m_types.pointer(t);
    }
    if (op == "++" || op == "--") {
        if (!e->kids[0]->semaLvalue) { error(e->loc, "S108", "operand of " + op + " is not an lvalue"); return m_types.error(); }
        if (t->isConst) { error(e->loc, "S104", "cannot modify a const value"); return m_types.error(); }
        if (!t->isArithmetic() && t->kind != Type::Pointer || t->kind == Type::Bool && op == "--") { error(e->loc, "S109", "invalid operand for " + op + ": " + t->text()); return m_types.error(); }
        e->semaLvalue = true;
        return t;
    }
    const Type* v = valueType(e->kids[0]);
    if (op == "!") {
        if (v->kind == Type::Class) { e->kids[0] = boolContext(e->kids[0], in); v = valueType(e->kids[0]); }
        if (!v->isScalar()) { error(e->loc, "S109", "operand of ! must be scalar, not " + v->text()); return m_types.error(); }
        if (e->kids[0]->semaConst) { e->semaConst = true; e->semaValue = !e->kids[0]->semaValue; }
        return m_types.basic(Type::Bool);
    }
    if (op == "~") {
        if (!v->isIntegral()) { error(e->loc, "S109", "operand of ~ must be integral, not " + v->text()); return m_types.error(); }
        if (e->kids[0]->semaConst) { e->semaConst = true; e->semaValue = ~e->kids[0]->semaValue; }
        return m_types.promoted(v);
    }
    if (!v->isArithmetic()) { error(e->loc, "S109", "operand of unary " + op + " must be arithmetic, not " + v->text()); return m_types.error(); }
    if (e->kids[0]->semaConst) { e->semaConst = true; e->semaValue = op == "-" ? -e->kids[0]->semaValue : e->kids[0]->semaValue; }
    const Type* uv = m_types.unqualified(m_types.unref(v));
    return uv->kind == Type::Float ? uv : m_types.promoted(v);   // the integral promotions only: -f is a float
}

// a class object in a boolean context (conditions, !, &&, ||): wrapped in a cast node that calls its
// conversion operator, so later stages see an ordinary scalar operand
cxx::Expr* Sema::boolContext(cxx::Expr* e, Scope* in)
{
    const Type* t = valueType(e);
    if (!t || t->kind != Type::Class || !t->decl) return e;
    Conversion c = convertType(t, e->semaLvalue, false, m_types.basic(Type::Bool), in, true);
    if (c.rank == Conversion::None || !c.userFunction) { error(e->loc, "S109", "no conversion from " + t->text() + " to bool"); return e; }
    cxx::Expr* w = nodeArena().expr(cxx::Expr::Cast, e->loc);
    w->kids.push_back(e);
    w->semaSym = c.userFunction;
    noteUse(c.userFunction);
    w->semaType = c.userFunction->type && c.userFunction->type->kind == Type::Function ? c.userFunction->type->inner : m_types.basic(Type::Bool);
    return w;
}

// the arguments that match "..." of the function type ft get the default argument promotions (5.2.2/7): float
// becomes double and the integral promotions apply; a cast node to the promoted type makes it explicit for the
// later stages (a float travelled as its own bits)
void Sema::promoteVariadic(std::vector<cxx::Expr*>& args, size_t firstArg, const Type* ft)
{
    if (!ft || ft->kind != Type::Function || !ft->variadic) return;
    for (size_t a = firstArg + ft->params.size(); a < args.size(); ++a) {
        cxx::Expr* x = args[a];
        if (!x || !x->semaType) continue;
        const Type* v = valueType(x);
        if (!v->isArithmetic()) continue;
        const Type* p = m_types.promoted(v);
        if (p == v) continue;
        cxx::Expr* w = nodeArena().expr(cxx::Expr::Cast, x->loc);
        w->kids.push_back(x);
        w->semaType = p;
        if (x->semaConst && p->isIntegral()) { w->semaConst = true; w->semaValue = x->semaValue; }
        args[a] = w;
    }
}

// an operand converted through a conversion operator: wrapped in a cast node that calls it (a converting
// constructor is applied by the IR generator where the object is built, so the operand stays as it is)
cxx::Expr* Sema::converted(cxx::Expr* e, const Conversion& c)
{
    noteUse(c.userFunction);                    // a conversion operator, or a converting constructor
    if (!c.userFunction || !c.userFunction->isConversion) return e;
    cxx::Expr* w = nodeArena().expr(cxx::Expr::Cast, e->loc);
    w->kids.push_back(e);
    w->semaSym = c.userFunction;
    w->semaType = c.userFunction->type && c.userFunction->type->kind == Type::Function ? c.userFunction->type->inner : m_types.error();
    if (w->semaType->kind == Type::Reference) w->semaLvalue = true;
    return w;
}

const Type* Sema::exprBinary(cxx::Expr* e, Scope* in)
{
    const std::string& op = e->text;
    const Type* lt0 = expr(e->kids[0], in);
    const Type* rt0 = expr(e->kids[1], in);
    const Type* l = valueType(e->kids[0]);
    const Type* r = valueType(e->kids[1]);
    (void)lt0; (void)rt0;
    if (l->kind == Type::Dependent || r->kind == Type::Dependent) return m_types.dependent(0);
    if (l->kind == Type::Error || r->kind == Type::Error) return m_types.error();
    if ((op == "&&" || op == "||") && (l->kind == Type::Class || r->kind == Type::Class)) {
        e->kids[0] = boolContext(e->kids[0], in);
        e->kids[1] = boolContext(e->kids[1], in);
        l = valueType(e->kids[0]);
        r = valueType(e->kids[1]);
    }
    if ((op == ".*" || op == "->*") && r->kind == Type::MemberPointer) {
        // the object's class is the member's class or derived from it; a data member yields an lvalue with the
        // object's qualifiers, a function the function type (only callable)
        const Type* objQ = op == "->*" ? (l->kind == Type::Pointer ? l->inner : 0) : m_types.unref(e->kids[0]->semaType);
        const Type* obj = objQ ? m_types.unqualified(objQ) : 0;
        if (!obj || obj->kind != Type::Class || !obj->decl) { error(e->loc, "S109", "left operand of " + op + " is not " + std::string(op == "->*" ? "a pointer to " : "") + "an object of class type"); return m_types.error(); }
        Symbol* mc = r->memberClass && r->memberClass->kind == Type::Class ? r->memberClass->decl : 0;
        if (mc && obj->decl != mc && !derivesFrom(obj->decl, mc, 0)) { error(e->loc, "S105", "cannot apply a pointer to member of '" + mc->qualifiedName() + "' to an object of '" + obj->decl->qualifiedName() + "'"); return m_types.error(); }
        if (!r->inner) return m_types.error();
        if (r->inner->kind == Type::Function) return r->inner;
        e->semaLvalue = true;
        return m_types.qualified(r->inner, objQ->isConst, objQ->isVolatile);
    }
    if (l->kind == Type::Class || r->kind == Type::Class) {
        const Type* res = 0;
        std::vector<cxx::Expr*> ops; ops.push_back(e->kids[0]); ops.push_back(e->kids[1]);
        Symbol* f = classOperator(op, e, ops, in, res);
        if (f) { e->semaSym = f; if (res->kind == Type::Reference) e->semaLvalue = true; return res; }
        // a class with a conversion operator to a scalar takes part in the builtin operators
        Conversion cl = l->kind == Type::Class ? convertType(l, true, false, m_types.basic(Type::Long), in, true) : Conversion();
        Conversion cr = r->kind == Type::Class ? convertType(r, true, false, m_types.basic(Type::Long), in, true) : Conversion();
        bool okL = l->kind != Type::Class || (cl.rank != Conversion::None && cl.userFunction);
        bool okR = r->kind != Type::Class || (cr.rank != Conversion::None && cr.userFunction);
        if (okL && okR) {
            if (l->kind == Type::Class) { l = m_types.unref(cl.userFunction->type->inner); e->kids[0] = converted(e->kids[0], cl); }
            if (r->kind == Type::Class) { r = m_types.unref(cr.userFunction->type->inner); e->kids[1] = converted(e->kids[1], cr); }
        } else {
            error(e->loc, "S109", "no operator" + op + " for " + l->text() + " and " + r->text());
            return m_types.error();
        }
    }
    bool lc = e->kids[0]->semaConst, rc = e->kids[1]->semaConst;
    long lv = e->kids[0]->semaValue, rv = e->kids[1]->semaValue;
    if (op == "&&" || op == "||") {
        if (!l->isScalar() || !r->isScalar()) { error(e->loc, "S109", "operands of " + op + " must be scalar"); return m_types.error(); }
        if (lc && rc) { e->semaConst = true; e->semaValue = op == "&&" ? (lv && rv) : (lv || rv); }
        return m_types.basic(Type::Bool);
    }
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
        bool ok = (l->isArithmetic() && r->isArithmetic()) || (l->isPointer() && r->isPointer()) || (l->isPointer() && isNullConstant(e->kids[1])) || (isNullConstant(e->kids[0]) && r->isPointer())
                  || (l->kind == Type::MemberPointer && (r->kind == Type::MemberPointer || isNullConstant(e->kids[1])))
                  || (isNullConstant(e->kids[0]) && r->kind == Type::MemberPointer);
        if (ok && l->kind == Type::MemberPointer && r->kind == Type::MemberPointer && (op != "==" && op != "!=")) ok = false;
        if (!ok) { error(e->loc, "S109", "cannot compare " + l->text() + " with " + r->text()); return m_types.error(); }
        if (l->isIntegral() && r->isIntegral() && l->isUnsigned() != r->isUnsigned() && !lc && !rc && op != "==" && op != "!=") warning(e->loc, "S120", "comparison of signed and unsigned values");
        if (lc && rc) {
            e->semaConst = true;
            if (l->isArithmetic() && r->isArithmetic() && m_types.arithmeticResult(l, r)->isUnsigned()) {
                unsigned long a = static_cast<unsigned long>(lv), b = static_cast<unsigned long>(rv);
                e->semaValue = op == "==" ? a == b : op == "!=" ? a != b : op == "<" ? a < b : op == ">" ? a > b : op == "<=" ? a <= b : a >= b;
            } else e->semaValue = op == "==" ? lv == rv : op == "!=" ? lv != rv : op == "<" ? lv < rv : op == ">" ? lv > rv : op == "<=" ? lv <= rv : lv >= rv;
        }
        return m_types.basic(Type::Bool);
    }
    if (op == "+" || op == "-") {
        if (l->isArithmetic() && r->isArithmetic()) {
            const Type* rt = m_types.arithmeticResult(l, r);
            if (lc && rc) { e->semaConst = true; e->semaValue = op == "+" ? lv + rv : lv - rv; }
            if (lc && rc && rt->isIntegral() && m_types.sizeOf(rt) == 8) foldWide(e, op, lv, rv, rt->isUnsigned());
            return rt;
        }
        if (l->kind == Type::Pointer && r->isIntegral()) return l;
        if (op == "+" && l->isIntegral() && r->kind == Type::Pointer) return r;
        if (op == "-" && l->kind == Type::Pointer && r->kind == Type::Pointer) return m_types.ptrdiffType();
        error(e->loc, "S109", "invalid operands for " + op + ": " + l->text() + " and " + r->text());
        return m_types.error();
    }
    if (op == "*" || op == "/" || op == "%") {
        bool ok = op == "%" ? (l->isIntegral() && r->isIntegral()) : (l->isArithmetic() && r->isArithmetic());
        if (!ok) { error(e->loc, "S109", "invalid operands for " + op + ": " + l->text() + " and " + r->text()); return m_types.error(); }
        if (lc && rc) {
            if ((op == "/" || op == "%") && rv == 0) warning(e->loc, "S121", "division by zero");
            else if (op != "*" && m_types.arithmeticResult(l, r)->isUnsigned()) {
                unsigned long a = static_cast<unsigned long>(lv), b = static_cast<unsigned long>(rv);
                e->semaConst = true; e->semaValue = static_cast<long>(op == "/" ? a / b : a % b);
            } else { e->semaConst = true; e->semaValue = op == "*" ? lv * rv : op == "/" ? lv / rv : lv % rv; }
        }
        const Type* rt = m_types.arithmeticResult(l, r);
        if (e->semaConst && rt->isIntegral() && m_types.sizeOf(rt) == 8) foldWide(e, op, lv, rv, rt->isUnsigned());
        return rt;
    }
    if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
        if (!l->isIntegral() || !r->isIntegral()) { error(e->loc, "S109", "operands of " + op + " must be integral, not " + l->text() + " and " + r->text()); return m_types.error(); }
        if (lc && rc) {
            e->semaConst = true;
            if (op == ">>" && m_types.promoted(l)->isUnsigned()) e->semaValue = static_cast<long>(static_cast<unsigned long>(lv) >> rv);
            else e->semaValue = op == "&" ? (lv & rv) : op == "|" ? (lv | rv) : op == "^" ? (lv ^ rv) : op == "<<" ? static_cast<long>(static_cast<unsigned long>(lv) << rv) : (lv >> rv);
        }
        const Type* rt = (op == "<<" || op == ">>") ? m_types.promoted(l) : m_types.arithmeticResult(l, r);
        if (lc && rc && m_types.sizeOf(rt) == 8) foldWide(e, op, lv, rv, rt->isUnsigned());
        return rt;
    }
    if (op == ".*" || op == "->*") {
        const Type* obj = op == "->*" ? (l->kind == Type::Pointer ? l->inner : l) : m_types.unref(e->kids[0]->semaType);
        if (r->kind != Type::MemberPointer) { error(e->loc, "S109", "right operand of " + op + " is not a pointer to member"); return m_types.error(); }
        (void)obj;
        e->semaLvalue = true;
        return r->inner;
    }
    error(e->loc, "S109", "unknown operator " + op);
    return m_types.error();
}

const Type* Sema::exprAssign(cxx::Expr* e, Scope* in)
{
    const std::string& op = e->text;
    const Type* lt = expr(e->kids[0], in);
    expr(e->kids[1], in);
    const Type* l = m_types.unref(lt);
    if (l->kind == Type::Dependent || l->kind == Type::Error) return l;
    const Type* r = valueType(e->kids[1]);
    if (r->kind == Type::Dependent) return l;
    if (l->kind == Type::Class) {
        const Type* res = 0;
        std::vector<cxx::Expr*> ops; ops.push_back(e->kids[0]); ops.push_back(e->kids[1]);
        Symbol* f = classOperator(op, e, ops, in, res);
        if (f && f->isImplicit && !f->deletedWhy.empty() && l->decl) { error(e->loc, "S152", "'" + l->decl->qualifiedName() + "::operator=' cannot be generated: '" + l->decl->name + "' has " + f->deletedWhy + " (cl C2582)"); return m_types.error(); }
        if (f && f->ownerClass && !f->isImplicit) accessCheck(f, e->loc, in);
        if (f) { e->semaSym = f; e->semaLvalue = true; return res; }
        error(e->loc, "S109", "no operator" + op + " for " + l->text() + " and " + r->text());
        return m_types.error();
    }
    if (!e->kids[0]->semaLvalue) { error(e->loc, "S108", "left side of " + op + " is not an lvalue"); return m_types.error(); }
    if (l->isConst) { error(e->loc, "S104", "assignment to a const value of type " + l->text()); return m_types.error(); }
    if (l->kind == Type::Array) { error(e->loc, "S104", "an array cannot be assigned"); return m_types.error(); }
    if (op == "=") {
        Conversion c = convert(e->kids[1], l, in);
        if (c.rank == Conversion::None && r->kind != Type::Error) error(e->loc, "S105", "cannot convert " + r->text() + " to " + l->text() + " in assignment");
        narrowingWarning(e->kids[1], l, "=");
        e->kids[1] = converted(e->kids[1], c);
    } else {
        std::string bop = op.substr(0, op.size() - 1);
        bool ok;
        if (bop == "+" || bop == "-") ok = (l->isArithmetic() && r->isArithmetic()) || (l->kind == Type::Pointer && r->isIntegral());
        else if (bop == "*" || bop == "/") ok = l->isArithmetic() && r->isArithmetic();
        else ok = l->isIntegral() && r->isIntegral();
        if (!ok && r->kind != Type::Error) error(e->loc, "S109", "invalid operands for " + op + ": " + l->text() + " and " + r->text());
    }
    e->semaLvalue = true;
    return l;
}

const Type* Sema::expr(cxx::Expr* e, Scope* in)
{
    if (!e) return m_types.error();
    if (e->semaType) return e->semaType;
    ++m_stats.expressionsTyped;
    // the compiler runs on a 512 MB stack (driver/main.cpp): an expression nested deeper than this is refused, not
    // silently typed as an error (cl's limits lie beyond 5000 parentheses)
    if (++m_depth > 50000) { --m_depth; if (m_depth == 50000) error(e->loc, "S149", "expression nested too deeply (more than 50000 levels)"); return e->semaType = m_types.error(); }
    const Type* t = m_types.error();
    switch (e->kind) {
    case cxx::Expr::IntLit: {
        std::string s = e->text;
        bool u = false; int l = 0;
        while (!s.empty() && (s[s.size() - 1] == 'u' || s[s.size() - 1] == 'U' || s[s.size() - 1] == 'l' || s[s.size() - 1] == 'L')) { if (s[s.size() - 1] == 'u' || s[s.size() - 1] == 'U') u = true; else ++l; s.erase(s.size() - 1); }
        // the type is the first of int, (unsigned int for hexadecimal and octal), long, unsigned long, long
        // long, unsigned long long that holds the value, as cl does
        bool overflow = false;
        const unsigned long long v = integerLiteralValue(e->text, &overflow);
        const bool hexOct = s.size() > 1 && s[0] == '0';
        const bool wide = v > 0xFFFFFFFFULL;
        const bool fitsSigned = v <= 0x7FFFFFFFULL;
        Type::Kind k;
        if (l >= 2 || wide) k = u || v > 0x7FFFFFFFFFFFFFFFULL ? Type::ULongLong : Type::LongLong;
        else if (u) k = l ? Type::ULong : Type::UInt;
        else if (l) k = fitsSigned ? Type::Long : Type::ULong;
        else k = fitsSigned ? Type::Int : (hexOct ? Type::UInt : Type::ULong);
        if (overflow) warning(e->loc, "S127", "integer constant " + e->text + " does not fit in 64 bits: only its low 64 bits are kept");
        // semaValue holds 32 bits, sign-extended for a 64-bit type: a 64-bit value outside that range is no
        // constant here (IrGen builds it at run time; a constant expression needing it is an error)
        const long long sv = static_cast<long long>(v);
        e->semaConst = !(k == Type::LongLong || k == Type::ULongLong) || (sv >= -2147483647LL - 1 && sv <= 2147483647LL);
        e->semaValue = e->semaConst ? static_cast<long>(static_cast<int>(static_cast<unsigned int>(v & 0xFFFFFFFFULL))) : 0;
        t = m_types.basic(k);
        break;
    }
    case cxx::Expr::FloatLit: { char last = e->text.empty() ? 0 : e->text[e->text.size() - 1]; t = m_types.basic(last == 'f' || last == 'F' ? Type::Float : last == 'l' || last == 'L' ? Type::LongDouble : Type::Double); break; }
    case cxx::Expr::CharLit: {
        // char is signed, as with cl; a multicharacter constant ('ab') is an int
        int count = 0;
        e->semaConst = true;
        e->semaValue = charLiteralValue(e->text, count);
        t = m_types.basic(e->text[0] == 'L' ? Type::WChar : count > 1 ? Type::Int : Type::Char);
        break;
    }
    case cxx::Expr::StringLit: {
        std::vector<unsigned long> units;
        const bool wide = stringLiteralUnits(e->text, units);
        const long n = static_cast<long>(units.size());
        t = m_types.array(m_types.qualified(m_types.basic(wide ? Type::WChar : Type::Char), true, false), n + 1);
        e->semaLvalue = true;
        break;
    }
    case cxx::Expr::BoolLit: t = m_types.basic(Type::Bool); e->semaConst = true; e->semaValue = e->text == "true"; break;
    case cxx::Expr::This: {
        Symbol* cls = m_currentClass;
        if (!cls || !m_currentFunction || m_currentFunction->isStatic) { error(e->loc, "S122", "'this' outside a non-static member function"); break; }
        const Type* ct = m_types.classType(cls);
        if (m_currentFunction->type && m_currentFunction->type->constMethod) ct = m_types.qualified(ct, true, false);
        t = m_types.pointer(ct);
        break;
    }
    case cxx::Expr::Name: t = exprName(e, in); break;
    case cxx::Expr::Paren: t = expr(e->kids[0], in); e->semaLvalue = e->kids[0]->semaLvalue; e->semaConst = e->kids[0]->semaConst; e->semaValue = e->kids[0]->semaValue; e->semaSym = e->kids[0]->semaSym; break;
    case cxx::Expr::Unary: t = exprUnary(e, in); break;
    case cxx::Expr::PostIncDec: {
        const Type* ot = m_types.unref(expr(e->kids[0], in));
        if (ot->kind == Type::Class) {
            // the postfix form is operator++(int): a literal 0 is its second operand (kept as kids[1])
            if (e->kids.size() < 2) {
                cxx::Expr* zero = nodeArena().expr(cxx::Expr::IntLit, e->loc);
                zero->text = "0";
                expr(zero, in);
                e->kids.push_back(zero);
            }
            const Type* r = 0;
            std::vector<cxx::Expr*> ops; ops.push_back(e->kids[0]); ops.push_back(e->kids[1]);
            Symbol* f = classOperator(e->text, e, ops, in, r);
            if (f) { e->semaSym = f; t = r; } else error(e->loc, "S109", "no postfix operator" + e->text + " for " + ot->text());
            break;
        }
        if (ot->kind == Type::Dependent || ot->kind == Type::Error) { t = ot; break; }
        if (!e->kids[0]->semaLvalue) { error(e->loc, "S108", "operand of " + e->text + " is not an lvalue"); break; }
        if (ot->isConst) { error(e->loc, "S104", "cannot modify a const value"); break; }
        t = m_types.unqualified(ot);
        break;
    }
    case cxx::Expr::Binary: t = exprBinary(e, in); break;
    case cxx::Expr::Assign: t = exprAssign(e, in); break;
    case cxx::Expr::Cond: {
        expr(e->kids[0], in); e->kids[0] = boolContext(e->kids[0], in);
        const Type* a = expr(e->kids[1], in); const Type* b = expr(e->kids[2], in);
        const Type* av = valueType(e->kids[1]); const Type* bv = valueType(e->kids[2]);
        if (!valueType(e->kids[0])->isScalar() && valueType(e->kids[0])->kind != Type::Dependent && valueType(e->kids[0])->kind != Type::Error) error(e->loc, "S109", "condition of ?: must be scalar");
        if (av->kind == Type::Dependent || bv->kind == Type::Dependent) { t = av; break; }
        const Type* ar = m_types.unref(a);
        const Type* br = m_types.unref(b);
        if (e->kids[1]->semaLvalue && e->kids[2]->semaLvalue && (ar == br || (ar->kind == Type::Class && m_types.unqualified(ar) == m_types.unqualified(br)))) {
            // two lvalues of one class type that differ in cv-qualification: an lvalue of the more qualified type
            // (scalars stay values: a folded constant such as string::npos has no storage)
            e->semaLvalue = true;
            t = m_types.qualified(m_types.unqualified(ar), ar->isConst || br->isConst, ar->isVolatile || br->isVolatile);
        }
        else if (av == bv) t = av;
        else if (av->isArithmetic() && bv->isArithmetic()) t = m_types.arithmeticResult(av, bv);
        else if (av->isPointer() && (bv->isPointer() || isNullConstant(e->kids[2]))) t = av->kind == Type::NullPtr ? bv : av;
        else if (bv->isPointer() && isNullConstant(e->kids[1])) t = bv;
        else if (av->kind == Type::Class && bv->kind == Type::Class && av->decl && bv->decl && (derivesFrom(av->decl, bv->decl, 0) || derivesFrom(bv->decl, av->decl, 0))) t = derivesFrom(av->decl, bv->decl, 0) ? bv : av;
        else if (av->kind == Type::Error || bv->kind == Type::Error) t = m_types.error();
        else { Conversion c1 = convert(e->kids[2], av, in); Conversion c2 = convert(e->kids[1], bv, in); if (c1.rank != Conversion::None) t = av; else if (c2.rank != Conversion::None) t = bv; else error(e->loc, "S109", "incompatible operands in ?: " + av->text() + " and " + bv->text()); }
        if (e->kids[0]->semaConst && e->kids[1]->semaConst && e->kids[2]->semaConst) { e->semaConst = true; e->semaValue = e->kids[0]->semaValue ? e->kids[1]->semaValue : e->kids[2]->semaValue; }
        break;
    }
    case cxx::Expr::Comma: {
        expr(e->kids[0], in); t = expr(e->kids[1], in);
        const Type* l = valueType(e->kids[0]); const Type* r = valueType(e->kids[1]);
        if (l->kind == Type::Class || r->kind == Type::Class) {
            // a user-defined operator, (13.3.1.2: a member of the left operand's class, or a free function found by
            // ordinary and argument-dependent lookup) takes the place of the built-in comma
            const Type* res = 0;
            std::vector<cxx::Expr*> ops; ops.push_back(e->kids[0]); ops.push_back(e->kids[1]);
            Symbol* f = classOperator(",", e, ops, in, res);
            if (f) { e->semaSym = f; e->semaLvalue = res->kind == Type::Reference; t = res; break; }
        }
        e->semaLvalue = e->kids[1]->semaLvalue;
        break;
    }
    case cxx::Expr::Call: t = exprCall(e, in); break;
    case cxx::Expr::Index: {
        const Type* bt = expr(e->kids[0], in); expr(e->kids[1], in);
        const Type* b = m_types.unref(bt); const Type* i = valueType(e->kids[1]);
        if (b->kind == Type::Class) { const Type* r = 0; std::vector<cxx::Expr*> ops; ops.push_back(e->kids[0]); ops.push_back(e->kids[1]); Symbol* f = classOperator("[]", e, ops, in, r); if (f) { e->semaSym = f; t = r; if (r->kind == Type::Reference) e->semaLvalue = true; } else error(e->loc, "S109", "no operator[] for " + b->text()); break; }
        if (b->kind == Type::Dependent || i->kind == Type::Dependent) { t = m_types.dependent(0); break; }
        const Type* bv = valueType(e->kids[0]);
        if (bv->kind == Type::Pointer && i->isIntegral()) { t = bv->inner; e->semaLvalue = true; }
        else if (i->kind == Type::Pointer && bv->isIntegral()) { t = i->inner; e->semaLvalue = true; }
        else if (bv->kind != Type::Error && i->kind != Type::Error) error(e->loc, "S109", "cannot index " + bv->text() + " with " + i->text());
        break;
    }
    case cxx::Expr::Member: t = exprMember(e, in); break;
    case cxx::Expr::Cast: case cxx::Expr::NamedCast: case cxx::Expr::FunctionalCast: {
        if (e->kind == cxx::Expr::FunctionalCast && e->type && e->type->spec.kind == cxx::DeclSpec::Named && (!e->type->decl || e->type->decl->parts.empty())) {
            // "f(args)" parsed as a cast because f is also a type: a visible function of that name hides the type
            std::vector<Symbol*> all;
            resolveName(e->type->spec.name, in, false, &all);
            bool isFunction = false;
            for (size_t k = 0; k < all.size(); ++k) if ((all[k]->kind == Symbol::Function && !all[k]->isConstructor) || all[k]->kind == Symbol::FunctionTemplate) isFunction = true;   // inside a class its constructors carry the class name
            if (isFunction) {
                cxx::Expr* callee = nodeArena().expr(cxx::Expr::Name, e->loc);
                callee->name = e->type->spec.name;
                e->kind = cxx::Expr::Call;
                e->kids.insert(e->kids.begin(), callee);
                e->type = 0;
                t = exprCall(e, in);
                break;
            }
        }
        const Type* to = typeOf(e->type->spec, in);
        if (e->type->decl) to = applyDeclarator(to, *e->type->decl, in, 0);
        t = exprCast(e, in, to, e->kind == cxx::Expr::FunctionalCast);
        if (e->kind != cxx::Expr::FunctionalCast && e->kids.size() == 1 && e->kids[0]->semaConst && to->isIntegral() && !widensUnsigned(e->kids[0]->semaType, m_types.unref(to), e->kids[0]->semaValue)) { e->semaConst = true; e->semaValue = fitTo(e->kids[0]->semaValue, m_types.unref(to)); }
        if (to->kind == Type::Reference) e->semaLvalue = true;
        break;
    }
    case cxx::Expr::SizeofExpr: { expr(e->kids[0], in); const Type* ot = m_types.unref(e->kids[0]->semaType); t = m_types.sizeType(); memberPointerSizeUse(ot); if (ot->kind != Type::Dependent && ot->kind != Type::Error && completeForSizeof(ot, in, e->loc)) { e->semaConst = true; e->semaValue = m_types.sizeOf(ot); } break; }
    case cxx::Expr::SizeofType: {
        const Type* st = typeOf(e->type->spec, in);
        if (e->type->decl) st = applyDeclarator(st, *e->type->decl, in, 0);
        st = m_types.unref(st);                 // sizeof(T&) is sizeof(T) (5.3.3/2)
        t = m_types.sizeType();
        memberPointerSizeUse(st);
        if (st->kind != Type::Dependent && st->kind != Type::Error && completeForSizeof(st, in, e->loc)) { e->semaConst = true; e->semaValue = m_types.sizeOf(st); }
        break;
    }
    case cxx::Expr::New: t = exprNew(e, in); break;
    case cxx::Expr::Delete: { expr(e->kids[0], in); const Type* pt = valueType(e->kids[0]); if (pt->kind != Type::Pointer && pt->kind != Type::Dependent && pt->kind != Type::Error) error(e->loc, "S109", "delete of a non-pointer of type " + pt->text()); t = m_types.basic(Type::Void); break; }
    case cxx::Expr::Throw: if (!e->kids.empty()) expr(e->kids[0], in); t = m_types.basic(Type::Void); break;
    case cxx::Expr::Typeid: {
        // 5.2.8: an lvalue of type const std::type_info; typeid(type-id) and typeid(expr) of a non-polymorphic
        // operand name the static type (IrGen evaluates the operand only when it is a polymorphic class lvalue)
        if (e->type) { const Type* ot = typeOf(e->type->spec, in); if (e->type->decl) ot = applyDeclarator(ot, *e->type->decl, in, 0); e->semaOperand = ot; }
        else if (!e->kids.empty()) { expr(e->kids[0], in); e->semaOperand = e->kids[0]->semaType; }
        Symbol* ti = stdClass("type_info");
        if (!ti) { error(e->loc, "S146", "typeid needs std::type_info: include <typeinfo> (cl C2027)"); t = m_types.error(); break; }
        t = m_types.reference(m_types.qualified(m_types.classType(ti), true, false));
        e->semaLvalue = true;
        break;
    }
    case cxx::Expr::InitList: error(e->loc, "S105", "a braced list is not an expression"); t = m_types.error(); break;
    case cxx::Expr::Error: t = m_types.error(); break;
    }
    --m_depth;
    e->semaType = t ? t : m_types.error();
    noteUse(e->semaSym);                        // a call, an operator, a named or address-taken function
    return e->semaType;
}

void Sema::localDecl(cxx::Decl* d, Scope* in, bool isHandler)
{
    if (d->kind == cxx::Decl::Class) { declareClass(d->classDef, in, cxx::AccessPublic, 0); return; }
    if (d->kind == cxx::Decl::Enum) { declareEnum(d->enumDef, in, cxx::AccessPublic); return; }
    if (d->kind != cxx::Decl::Simple) { declare(d, in, cxx::AccessPublic); return; }
    Symbol* defined = 0;
    if (d->spec.kind == cxx::DeclSpec::Class && d->spec.classDef) defined = declareClass(d->spec.classDef, in, cxx::AccessPublic, 0);
    else if (d->spec.kind == cxx::DeclSpec::Enum && d->spec.enumDef) defined = declareEnum(d->spec.enumDef, in, cxx::AccessPublic);
    const Type* base = defined ? m_types.qualified(defined->kind == Symbol::Class ? m_types.classType(defined) : m_types.enumType(defined), d->spec.isConst, d->spec.isVolatile) : typeOf(d->spec, in);
    for (size_t i = 0; i < d->declarators.size(); ++i) {
        cxx::InitDeclarator& id = d->declarators[i];
        cxx::Declarator* dcl = id.decl;
        if (!dcl) continue;
        std::vector<Symbol*> params;
        const Type* t = completedArray(applyDeclarator(base, *dcl, in, &params), id.init);
        const std::string& name = dcl->name.parts.empty() ? std::string("") : dcl->name.parts.back().ident;
        if (d->spec.isTypedef) { Symbol* td = m_prog.newSymbol(Symbol::Typedef, name, in); td->type = t; td->loc = dcl->loc; dcl->semaSym = td; continue; }
        if (t->kind == Type::Function) { declareFunction(d, dcl, d->spec, in, cxx::AccessPublic, t, params); continue; }
        if (d->spec.isExtern && id.init.kind == cxx::Initializer::None) {
            // a block-scope extern declaration names the variable of the enclosing namespace (C++98 3.5/7): no
            // storage, no construction here; one not declared there yet is declared there
            Scope* ns = in;
            while (ns && !ns->isNamespaceLike()) ns = ns->parent;
            Symbol* global = 0;
            const std::vector<Symbol*>* found = ns ? ns->find(name) : 0;
            if (found) for (size_t k = 0; k < found->size() && !global; ++k) if ((*found)[k]->kind == Symbol::Variable) global = (*found)[k];
            if (!global && ns) {
                global = m_prog.newSymbol(Symbol::Variable, name, ns);
                global->type = t; global->loc = dcl->loc; global->isExtern = true;
                ++m_stats.variables;
            }
            if (global) { in->add(global); dcl->semaSym = global; continue; }
        }
        // C4456 / C4457: a local hiding a local or a parameter of the same function (the scopes up to the function's)
        for (Scope* s = in->parent; s && !name.empty(); s = s->parent) {
            if (s->kind != Scope::Block && s->kind != Scope::Function) break;
            const std::vector<Symbol*>* v = s->find(name);
            Symbol* hidden = 0;
            if (v) for (size_t k = 0; k < v->size() && !hidden; ++k) if ((*v)[k]->kind == Symbol::Variable && (*v)[k]->isLocal) hidden = (*v)[k];
            if (hidden) { warning(dcl->loc, "S158", "declaration of '" + name + "' hides " + (hidden->isParam ? "function parameter (cl C4457)" : "previous local declaration (cl C4456)")); break; }
            if (s->kind == Scope::Function) break;
        }
        Symbol* var = m_prog.newSymbol(Symbol::Variable, name, in);
        var->type = t; var->loc = dcl->loc; var->isLocal = true; var->isStatic = d->spec.isStatic; var->isExtern = d->spec.isExtern;
        var->declAlign = d->spec.align;
        if (t->kind != Type::Reference && !d->spec.isExtern) abstractCheck(t, dcl->loc);
        // cl realigns the stack frame for such a local; this back end lays locals out by their type only
        if (d->spec.align > 0 && !d->spec.isStatic && !d->spec.isExtern)
            warning(dcl->loc, "S129", "__declspec(align(" + intToString(d->spec.align) + ")) on a local variable is not supported and is ignored");
        dcl->semaSym = var;
        ++m_stats.variables;
        const Type* tu = m_types.unref(t);
        if (tu->kind == Type::Class && tu->decl && !tu->decl->isDefinition && t->kind != Type::Reference) error(dcl->loc, "S113", "variable of incomplete type '" + tu->text() + "'");
        if (!isHandler && !var->isStatic && !var->isExtern && t->kind != Type::Reference && tu->kind != Type::Class && tu->kind != Type::Error && tu->kind != Type::Dependent) {
            const Type* el = tu; while (el->kind == Type::Array) el = el->inner;
            if (el->kind != Type::Class) m_locals.push_back(var);
        }
        if (id.init.kind == cxx::Initializer::None) {
            if (isHandler || tu->kind == Type::Error) continue;
            if (t->kind == Type::Reference) error(dcl->loc, "S123", "reference '" + name + "' is not initialised");
            else if (tu->kind == Type::Class && tu->decl && tu->decl->isDefinition && !tu->decl->hasDefaultCtor && tu->decl->hasUserCtor) error(dcl->loc, "S107", "no default constructor for '" + tu->text() + "'");
            else if (tu->kind == Type::Class && tu->decl && tu->decl->isDefinition && tu->decl->hasUserCtor && !var->isStatic) {
                std::vector<Symbol*> ctors;
                lookupMember(tu->decl, tu->decl->ctorName(), ctors);
                std::vector<cxx::Expr*> none;
                Symbol* chosen = resolveOverload(ctors, none, 0, 0, in, dcl->loc, tu->decl->name + "::" + tu->decl->name, false);
                if (chosen) accessCheck(chosen, dcl->loc, in);
            }
            else if (t->isConst && t->kind != Type::Reference && tu->kind != Type::Class) error(dcl->loc, "S123", "const variable '" + name + "' is not initialised");
            continue;
        }
        if (id.init.kind == cxx::Initializer::Brace) { braceDeclInit(var, id.init, in); continue; }
        for (size_t a = 0; a < id.init.args.size(); ++a) expr(id.init.args[a], in);
        if (tu->kind == Type::Class && tu->decl && tu->decl->isDefinition && t->kind != Type::Reference) {
            std::vector<Symbol*> ctors;
            lookupMember(tu->decl, tu->decl->ctorName(), ctors);
            if (id.init.kind == cxx::Initializer::Assign && id.init.args.size() == 1) {
                Conversion c = convert(id.init.args[0], tu, in);
                if (c.rank == Conversion::None) error(dcl->loc, "S105", "cannot initialise '" + name + "' of type " + tu->text() + " from " + valueType(id.init.args[0])->text());
                else id.init.args[0] = converted(id.init.args[0], c);
            } else {
                var->initCtor = resolveOverload(ctors, id.init.args, 0, 0, in, dcl->loc, tu->decl->name + "::" + tu->decl->name, true);
                noteUse(var->initCtor);
                accessCheck(var->initCtor, dcl->loc, in);
            }
            continue;
        }
        if (tu->kind == Type::Error || tu->kind == Type::Dependent) { var->init = id.init.args.empty() ? 0 : id.init.args[0]; continue; }
        if (isStringInit(t, id.init)) {
            // char s[N] = "...": the literal's characters, then zeros; it must fit with its terminator, as in C++
            if (t->arraySize >= 0 && stringLiteralLength(id.init.args[0]->text) + 1 > t->arraySize) error(dcl->loc, "S105", "string literal too long for '" + name + "'");
            var->init = id.init.args[0];
            continue;
        }
        if (id.init.args.size() == 1) {
            Conversion c = convert(id.init.args[0], t, in);
            if (c.rank == Conversion::None && valueType(id.init.args[0])->kind != Type::Error) error(dcl->loc, "S105", "cannot initialise '" + name + "' of type " + t->text() + " from " + valueType(id.init.args[0])->text());
            narrowingWarning(id.init.args[0], t, "initializing");
            id.init.args[0] = converted(id.init.args[0], c);
            if (t->isConst && id.init.args[0]->semaConst && t->isIntegral()) { var->hasConstValue = true; var->constValue = id.init.args[0]->semaValue; }
        } else if (!id.init.args.empty()) error(dcl->loc, "S105", "too many initialisers for '" + name + "'");
        var->init = id.init.args.empty() ? 0 : id.init.args[0];
    }
}

void Sema::stmt(cxx::Stmt* s, Scope* in, Symbol* fn)
{
    if (!s) return;
    switch (s->kind) {
    case cxx::Stmt::Compound: {
        Scope* b = m_prog.newScope(Scope::Block, in, fn);
        for (size_t i = 0; i < s->kids.size(); ++i) stmt(s->kids[i], b, fn);
        break;
    }
    case cxx::Stmt::ExprStmt: expr(s->expr, in); break;
    case cxx::Stmt::DeclStmt: localDecl(s->decl, in); break;
    case cxx::Stmt::If: case cxx::Stmt::While: case cxx::Stmt::Switch: case cxx::Stmt::DoWhile: {
        Scope* b = m_prog.newScope(Scope::Block, in, fn);
        if (s->decl) localDecl(s->decl, b);
        else if (s->expr) {
            expr(s->expr, b);
            if (s->kind != cxx::Stmt::Switch && s->expr->kind == cxx::Expr::Assign && s->expr->text == "=") warning(s->expr->loc, "S159", "assignment used as a condition (cl C4706)");
            if (s->kind != cxx::Stmt::Switch) s->expr = boolContext(s->expr, b);
            const Type* ct = valueType(s->expr);
            if (!ct->isScalar() && ct->kind != Type::Error && ct->kind != Type::Dependent) error(s->loc, "S109", "condition of type " + ct->text() + " is not convertible to bool");
        }
        if (s->kind == cxx::Stmt::While || s->kind == cxx::Stmt::DoWhile) ++m_loopDepth;
        if (s->kind == cxx::Stmt::Switch) ++m_switchDepth;
        for (size_t i = 0; i < s->kids.size(); ++i) stmt(s->kids[i], b, fn);
        if (s->kind == cxx::Stmt::While || s->kind == cxx::Stmt::DoWhile) --m_loopDepth;
        if (s->kind == cxx::Stmt::Switch) --m_switchDepth;
        break;
    }
    case cxx::Stmt::For: {
        Scope* b = m_prog.newScope(Scope::Block, in, fn);
        if (s->init) stmt(s->init, b, fn);
        if (s->decl) localDecl(s->decl, b);
        else if (s->expr) {
            expr(s->expr, b);
            if (s->expr->kind == cxx::Expr::Assign && s->expr->text == "=") warning(s->expr->loc, "S159", "assignment used as a condition (cl C4706)");
            s->expr = boolContext(s->expr, b);
        }
        if (s->expr2) expr(s->expr2, b);
        ++m_loopDepth;
        for (size_t i = 0; i < s->kids.size(); ++i) stmt(s->kids[i], b, fn);
        --m_loopDepth;
        break;
    }
    case cxx::Stmt::Break: if (m_loopDepth == 0 && m_switchDepth == 0) error(s->loc, "S124", "break outside a loop or switch"); break;
    case cxx::Stmt::Continue: if (m_loopDepth == 0) error(s->loc, "S124", "continue outside a loop"); break;
    case cxx::Stmt::Return: {
        const Type* rt = fn && fn->type && fn->type->kind == Type::Function ? fn->type->inner : m_types.basic(Type::Void);
        if (s->expr) {
            expr(s->expr, in);
            if (rt->kind == Type::Void) { if (valueType(s->expr)->kind != Type::Void && valueType(s->expr)->kind != Type::Error) error(s->loc, "S125", "a void function returns a value"); }
            else if (rt->kind != Type::Dependent && rt->kind != Type::Error) {
                Conversion c = convert(s->expr, rt, in);
                if (c.rank == Conversion::None && valueType(s->expr)->kind != Type::Error && valueType(s->expr)->kind != Type::Dependent) error(s->loc, "S105", "cannot convert " + valueType(s->expr)->text() + " to the return type " + rt->text());
                narrowingWarning(s->expr, rt, "return");
                s->expr = converted(s->expr, c);
            }
        } else if (rt->kind != Type::Void && rt->kind != Type::Dependent && rt->kind != Type::Error && !(fn && (fn->isConstructor || fn->isDestructor))) error(s->loc, "S125", "return without a value in a function returning " + rt->text());
        break;
    }
    case cxx::Stmt::Goto: m_hasGoto = true; break;
    case cxx::Stmt::Asm: m_hasGoto = true; break;   // no flow warnings around assembler either
    case cxx::Stmt::Empty: case cxx::Stmt::Error: break;
    case cxx::Stmt::Label: case cxx::Stmt::Default: for (size_t i = 0; i < s->kids.size(); ++i) stmt(s->kids[i], in, fn); break;
    case cxx::Stmt::Case: {
        expr(s->expr, in);
        long v;
        if (!constantValue(s->expr, v) && valueType(s->expr)->kind != Type::Dependent) error(s->loc, "S114", "case label is not a constant");
        if (m_switchDepth == 0) error(s->loc, "S124", "case outside a switch");
        for (size_t i = 0; i < s->kids.size(); ++i) stmt(s->kids[i], in, fn);
        break;
    }
    case cxx::Stmt::Try: for (size_t i = 0; i < s->kids.size(); ++i) stmt(s->kids[i], in, fn); break;
    case cxx::Stmt::Handler: {
        Scope* b = m_prog.newScope(Scope::Block, in, fn);
        if (s->decl) localDecl(s->decl, b, true);
        for (size_t i = 0; i < s->kids.size(); ++i) stmt(s->kids[i], b, fn);
        break;
    }
    }
}

void Sema::analyzeFunctionBody(Symbol* fn, cxx::Decl* def, Scope* enclosing)
{
    Scope* fs = m_prog.newScope(Scope::Function, enclosing, fn);
    fn->body = fs;
    for (size_t i = 0; i < fn->params.size(); ++i) {
        Symbol* p = fn->params[i];
        if (!p->name.empty()) { p->scope = fs; fs->add(p); }
        p->isLocal = true;
    }
    Symbol* savedFn = m_currentFunction; Symbol* savedClass = m_currentClass;
    m_currentFunction = fn; m_currentClass = fn->ownerClass;
    int savedLoop = m_loopDepth, savedSwitch = m_switchDepth;
    m_loopDepth = 0; m_switchDepth = 0;
    std::vector<Symbol*> savedLocals; savedLocals.swap(m_locals);
    const bool savedGoto = m_hasGoto; m_hasGoto = false;
    const size_t diagMark = m_diags.size();
    // 10.4/3: no parameter or result of an abstract class (checked at the definition, as cl's C2259)
    for (size_t i = 0; i < fn->params.size(); ++i) if (fn->params[i]->type && fn->params[i]->type->kind != Type::Reference) abstractCheck(fn->params[i]->type, fn->loc);
    if (fn->type && fn->type->kind == Type::Function && fn->type->inner && fn->type->inner->kind != Type::Reference && fn->type->inner->kind != Type::Pointer) abstractCheck(fn->type->inner, fn->loc);
    for (size_t i = 0; i < def->memInits.size(); ++i) {
        cxx::MemInit& mi = def->memInits[i];
        for (size_t a = 0; a < mi.args.size(); ++a) expr(mi.args[a], fs);
        if (!fn->ownerClass) continue;
        std::vector<Symbol*> out;
        if (mi.name.parts.size() == 1) { const std::vector<Symbol*>* v = fn->ownerClass->members->find(mi.name.parts[0].ident); if (v) out = *v; }
        Symbol* target = out.empty() ? 0 : out[0];
        const Type* tt = 0;
        if (target && target->kind == Symbol::Variable) tt = target->type;
        else {
            const Type* bt = namedType(mi.name, fs, "", mi.loc);
            if (bt->kind == Type::Class) tt = bt;
            else if (bt->kind != Type::Dependent && bt->kind != Type::Error) { error(mi.loc, "S126", "'" + mi.name.text() + "' is neither a member nor a base"); continue; }
        }
        if (!tt) continue;
        const Type* tu = m_types.unref(tt);
        if (tu->kind == Type::Class && tu->decl && tu->decl->isDefinition) {
            std::vector<Symbol*> ctors; lookupMember(tu->decl, tu->decl->ctorName(), ctors);
            Symbol* chosen = resolveOverload(ctors, mi.args, 0, 0, fs, mi.loc, tu->decl->name + "::" + tu->decl->name, true);
            noteUse(chosen);
            mi.semaSym = chosen;
        } else if (mi.args.size() == 1) {
            Conversion c = convert(mi.args[0], tt, fs);
            if (c.rank == Conversion::None && valueType(mi.args[0])->kind != Type::Error) error(mi.loc, "S105", "cannot initialise '" + mi.name.text() + "' of type " + tt->text() + " from " + valueType(mi.args[0])->text());
        }
    }
    stmt(def->body, fs, fn);
    functionWarnings(fn, def, diagMark);
    m_locals.swap(savedLocals);
    m_hasGoto = savedGoto;
    m_loopDepth = savedLoop; m_switchDepth = savedSwitch;
    m_currentFunction = savedFn; m_currentClass = savedClass;
    ++m_stats.functionsDefined;
}

bool Sema::isLazyMember(const Symbol* fn) const
{
    if (fn->kind != Symbol::Function || fn->isVirtual) return false;   // the vtable needs every virtual member
    if (fn->isExplicitInstantiation) return false;                      // named by an explicit instantiation definition (14.7.2)
    // a function template specialisation exists only because something referred to it, and that something may
    // itself be a member nobody uses (Vector<Vector<double, 3>, 3>::normalized calling operator/)
    if (fn->instantiatedFrom && fn->instantiatedFrom->kind == Symbol::FunctionTemplate) return true;
    // a member of an implicit instantiation, or of a class nested in one
    for (const Symbol* c = fn->ownerClass; c; c = (c->scope && c->scope->kind == Scope::Class) ? c->scope->owner : 0)
        if (c->isImplicitInstantiation) return true;
    return false;
}

void Sema::noteUse(Symbol* used)
{
    if (used && used->kind == Symbol::Function) m_uses[m_currentFunction].insert(used);
}

const std::vector<Diag>* Sema::heldDiagnostics(Symbol* fn) const
{
    std::map<Symbol*, std::vector<Diag> >::const_iterator it = m_held.find(fn);
    return it == m_held.end() ? 0 : &it->second;
}

void Sema::reportUsedFailedMembers()
{
    if (m_held.empty()) return;
    // everything that is not a tentatively analysed member is real code; walk what it uses
    std::vector<Symbol*> work;
    std::set<Symbol*> reached;
    for (std::map<Symbol*, std::set<Symbol*> >::iterator it = m_uses.begin(); it != m_uses.end(); ++it)
        if (!m_lazyMembers.count(it->first)) work.push_back(it->first);
    while (!work.empty()) {
        Symbol* f = work.back(); work.pop_back();
        std::map<Symbol*, std::set<Symbol*> >::iterator u = m_uses.find(f);
        if (u == m_uses.end()) continue;
        for (std::set<Symbol*>::iterator g = u->second.begin(); g != u->second.end(); ++g)
            if (m_lazyMembers.count(*g) && reached.insert(*g).second) work.push_back(*g);
    }
    // a used member's errors are real (in symbol creation order, so the output does not depend on addresses)
    for (size_t i = 0; i < m_prog.symbols.size(); ++i) {
        Symbol* f = m_prog.symbols[i];
        if (!reached.count(f) || !m_held.count(f)) continue;
        const std::vector<Diag>& held = m_held[f];
        m_diags.insert(m_diags.end(), held.begin(), held.end());
    }
}

void Sema::analyzeBodies()
{
    // instantiations may queue more bodies while others are analysed
    for (size_t i = 0; i < m_pending.size(); ++i) {
        PendingBody pb = m_pending[i];
        m_bodyOrder = pb.order;
        if (pb.fn->kind == Symbol::Function) {
            if (pb.fn->body) continue;
            if (!isLazyMember(pb.fn)) { analyzeFunctionBody(pb.fn, pb.def, pb.scope); continue; }
            // tentative: errors are held back until the member turns out to be used
            m_lazyMembers.insert(pb.fn);
            pb.fn->isLazyInstantiation = true;
            size_t mark = m_diags.size();
            analyzeFunctionBody(pb.fn, pb.def, pb.scope);
            bool failed = false;
            for (size_t k = mark; k < m_diags.size(); ++k) if (m_diags[k].severity == Diag::Error) failed = true;
            if (failed) {
                pb.fn->instantiationFailed = true;
                m_held[pb.fn].assign(m_diags.begin() + static_cast<long>(mark), m_diags.end());
                m_diags.resize(mark);
            }
            continue;
        }
        // a namespace-scope or static member variable initializer
        Symbol* var = pb.fn;
        if (!var->init) continue;
        Scope* scope = var->ownerClass ? var->ownerClass->members : pb.scope;
        // every argument of "T x(a, b)": the declarator's initializer holds them all
        std::vector<cxx::Expr*> args;
        args.push_back(var->init);
        cxx::InitDeclarator* idp = 0;
        if (pb.def) for (size_t k = 0; k < pb.def->declarators.size(); ++k) if (pb.def->declarators[k].decl && pb.def->declarators[k].decl->semaSym == var && !pb.def->declarators[k].init.args.empty()) { idp = &pb.def->declarators[k]; args = idp->init.args; break; }
        Symbol* savedClass = m_currentClass;
        m_currentClass = var->ownerClass;
        if (idp && idp->init.kind == cxx::Initializer::Brace) {
            braceDeclInit(var, idp->init, scope);
            m_currentClass = savedClass;
            if (var->init && var->type->isConst && var->init->semaConst && var->type->isIntegral()) { var->hasConstValue = true; var->constValue = var->init->semaValue; }
            continue;
        }
        for (size_t k = 0; k < args.size(); ++k) expr(args[k], scope);
        m_currentClass = savedClass;
        const Type* tu = m_types.unref(var->type);
        if (tu->kind == Type::Class && tu->decl && tu->decl->isDefinition) {
            std::vector<Symbol*> ctors;
            lookupMember(tu->decl, tu->decl->ctorName(), ctors);
            var->initCtor = resolveOverload(ctors, args, 0, 0, scope, var->loc, tu->decl->name + "::" + tu->decl->name, true);
            noteUse(var->initCtor);
            accessCheck(var->initCtor, var->loc, scope);
        } else if (var->type->kind != Type::Array && var->init->kind != cxx::Expr::Error) {
            Conversion c = convert(var->init, var->type, pb.scope);
            if (c.rank == Conversion::None && valueType(var->init)->kind != Type::Error) error(var->loc, "S105", "cannot initialise '" + var->name + "' of type " + var->type->text() + " from " + valueType(var->init)->text());
            narrowingWarning(var->init, var->type, "initializing");
            cxx::Expr* w = converted(var->init, c);
            if (w != var->init) {
                if (pb.def) for (size_t k = 0; k < pb.def->declarators.size(); ++k) if (pb.def->declarators[k].decl && pb.def->declarators[k].decl->semaSym == var && !pb.def->declarators[k].init.args.empty()) pb.def->declarators[k].init.args[0] = w;
                var->init = w;
            }
        }
        if (var->type->isConst && var->init->semaConst && var->type->isIntegral()) { var->hasConstValue = true; var->constValue = var->init->semaValue; }
    }
}

// ---- access control (C++98 clause 11) ----
// a private member is accessible in the members and friends of its class, a protected one also in the members and
// friends of the classes derived from it (the naming-class rule of 11.5 is not applied: a derived member may use a
// protected member through any object of the base). The contexts are the classes enclosing the point of use: the
// class scopes above it, the class of the function being analysed and the classes they are nested in (a nested
// class is a member, as cl and DR 45 have it). A class whose friends could not be resolved is not checked
static Symbol* classOfMember(Symbol* member)
{
    if (member->ownerClass) return member->ownerClass;
    return member->scope && member->scope->kind == Scope::Class ? member->scope->owner : 0;
}

bool Sema::isFriendOf(Symbol* x, Symbol* cls)
{
    for (size_t i = 0; i < cls->friends.size(); ++i) {
        Symbol* f = cls->friends[i];
        if (f == x || (x->instantiatedFrom && f == x->instantiatedFrom)) return true;
    }
    if (!cls->friendNames.empty() && (x->kind == Symbol::Class || x->kind == Symbol::ClassTemplate)) {
        // "friend class List;" seen before List was declared: the class of that name (its template for an instantiation)
        Symbol* named = x->instantiatedFrom ? x->instantiatedFrom : x;
        for (size_t i = 0; i < cls->friendNames.size(); ++i)
            if (cls->friendNames[i] == named->name || cls->friendNames[i] == named->qualifiedName()) return true;
    }
    return false;
}

bool Sema::accessible(Symbol* member, Scope* from)
{
    if (!member || member->access == cxx::AccessPublic || member->access == cxx::AccessDefault) return true;
    Symbol* owner = classOfMember(member);
    if (!owner || owner->kind != Symbol::Class || owner->friendsUnknown || owner->isDependentContext) return true;
    Symbol* ownerTemplate = owner->instantiatedFrom;
    if (ownerTemplate && ownerTemplate->friendsUnknown) return true;
    std::vector<Symbol*> contexts;
    for (Scope* s = from; s; s = s->parent) {
        if ((s->kind == Scope::Class || s->kind == Scope::Instantiation) && s->owner && s->owner->kind == Symbol::Class) contexts.push_back(s->owner);
        if (s->kind == Scope::Function && s->owner && s->owner->ownerClass) contexts.push_back(s->owner->ownerClass);
    }
    if (m_currentClass) contexts.push_back(m_currentClass);
    if (m_currentFunction && m_currentFunction->ownerClass) contexts.push_back(m_currentFunction->ownerClass);
    for (size_t i = 0; i < contexts.size(); ++i)
        for (Scope* s = contexts[i]->scope; s; s = s->parent) if (s->kind == Scope::Class && s->owner && s->owner->kind == Symbol::Class) contexts.push_back(s->owner);
    for (size_t i = 0; i < contexts.size(); ++i) {
        Symbol* c = contexts[i];
        if (c == owner) return true;
        if (c->isDependentContext) return true;   // the body of a template: checked in its instantiations
        if (member->access == cxx::AccessProtected && derivesFrom(c, owner, 0)) return true;
        if (isFriendOf(c, owner) || (ownerTemplate && isFriendOf(c, ownerTemplate))) return true;
    }
    if (m_currentFunction && (isFriendOf(m_currentFunction, owner) || (ownerTemplate && isFriendOf(m_currentFunction, ownerTemplate)))) return true;
    return false;
}

void Sema::accessCheck(Symbol* member, const cxx::Loc& loc, Scope* from)
{
    if (!member || accessible(member, from)) return;
    Symbol* owner = classOfMember(member);
    error(loc, "S150", "'" + owner->qualifiedName() + "::" + member->name + "': cannot access " + (member->access == cxx::AccessPrivate ? "private" : "protected") + " member declared in class '" + owner->qualifiedName() + "' (cl C2248)");
}

// 10.4/3: no object, parameter or result of an abstract class
void Sema::abstractCheck(const Type* t, const cxx::Loc& loc)
{
    if (!t) return;
    const Type* u = m_types.unqualified(t);
    while (u->kind == Type::Array) u = m_types.unqualified(u->inner);
    if (u->kind != Type::Class || !u->decl || !u->decl->isDefinition || !u->decl->isAbstract || u->decl->isDependentContext) return;
    std::string pure;
    for (size_t t2 = 0; t2 < u->decl->vftables.size() && pure.empty(); ++t2)
        for (size_t s = 0; s < u->decl->vftables[t2].fns.size() && pure.empty(); ++s) if (u->decl->vftables[t2].fns[s]->isPureVirtual) pure = u->decl->vftables[t2].fns[s]->name;
    error(loc, "S151", "'" + u->decl->qualifiedName() + "': cannot instantiate abstract class ('" + pure + "' is pure virtual) (cl C2259)");
}

bool Sema::assignmentDeleted(Symbol* cls)
{
    if (!cls || !cls->members) return false;
    const std::vector<Symbol*>* v = cls->members->find("operator=");
    if (!v) return false;
    for (size_t i = 0; i < v->size(); ++i) if ((*v)[i]->kind == Symbol::Function && (*v)[i]->isImplicit && !(*v)[i]->deletedWhy.empty()) return true;
    return false;
}

// cl's C4244 / C4305 / C4309 / C4245: an arithmetic value narrowed by an implicit conversion (a constant only when
// it does not fit); bool and enumerations aside
void Sema::narrowingWarning(cxx::Expr* from, const Type* to, const char* what)
{
    if (!from || !from->semaType || !to) return;
    if (from->kind == cxx::Expr::Paren && from->kids.size() == 1) { narrowingWarning(from->kids[0], to, what); return; }
    if (from->kind == cxx::Expr::Comma && from->kids.size() == 2) { narrowingWarning(from->kids[1], to, what); return; }
    if (from->kind == cxx::Expr::Cond && from->kids.size() == 3) { narrowingWarning(from->kids[1], to, what); narrowingWarning(from->kids[2], to, what); return; }
    const Type* f = valueType(from);
    const Type* t = m_types.unqualified(to);
    if (!f || t->kind == Type::Reference) return;
    if (!f->isArithmetic() || !t->isArithmetic() || t->kind == Type::Bool || f->kind == Type::Bool || f->kind == Type::Enum || t->kind == Type::Enum) return;
    const bool fFloat = f->isFloating(), tFloat = t->isFloating();
    const int fs = m_types.sizeOf(f), ts = m_types.sizeOf(t);
    const std::string ctx = std::string("'") + what + "': ";
    const std::string loss = ctx + "conversion from '" + f->text() + "' to '" + t->text() + "', possible loss of data (cl C4244)";
    if (fFloat && !tFloat) { warning(from->loc, "S160", loss); return; }
    if (fFloat && tFloat) {
        if (ts >= fs) return;
        double v;
        if (floatingConstant(from, v, true, 0)) warning(from->loc, "S160", ctx + "truncation from '" + f->text() + "' to '" + t->text() + "' (cl C4305)");
        else warning(from->loc, "S160", loss);
        return;
    }
    if (!fFloat && tFloat) return;
    if (ts < fs) {
        if (from->semaConst) {
            if (fs > 4) return;                              // the value travels in 32 bits: nothing to check
            const long v = from->semaValue;
            const int bits = ts * 8;
            const bool tUnsigned = t->isUnsigned() || t->kind == Type::WChar;
            const long lo = tUnsigned ? 0 : -(1L << (bits - 1));
            const long hi = tUnsigned ? (1L << bits) - 1 : (1L << (bits - 1)) - 1;
            if (v < lo || v > hi) warning(from->loc, "S160", ctx + "truncation of constant value (cl C4309)");
            return;
        }
        warning(from->loc, "S160", loss);
        return;
    }
    if (ts == fs && from->semaConst && !f->isUnsigned() && f->kind != Type::WChar && (t->isUnsigned() || t->kind == Type::WChar) && from->semaValue < 0)
        warning(from->loc, "S160", ctx + "conversion from '" + f->text() + "' to '" + t->text() + "', signed/unsigned mismatch (cl C4245)");
}

// ---- the warnings over a whole body: unreferenced parameters and locals (C4100, C4101, C4189), a control path
// without a return (C4715), unreachable code (C4702), an uninitialised local used (C4700) ----
void Sema::functionWarnings(Symbol* fn, cxx::Decl* def, size_t diagMark)
{
    for (size_t k = diagMark; k < m_diags.size(); ++k) if (m_diags[k].severity == Diag::Error) return;   // a body with errors: no warnings
    if (!def || !def->body || fn->isImplicit) return;
    for (size_t i = 0; i < fn->params.size(); ++i) {
        Symbol* p = fn->params[i];
        if (!p->name.empty() && !p->isUsed) warning(p->loc.line ? p->loc : fn->loc, "S153", "'" + p->name + "': unreferenced parameter (cl C4100)");
    }
    for (size_t i = 0; i < m_locals.size(); ++i) {
        Symbol* v = m_locals[i];
        if (v->isUsed || v->name.empty()) continue;
        warning(v->loc, "S154", "'" + v->name + (v->init ? "': local variable is initialized but not referenced (cl C4189)" : "': unreferenced local variable (cl C4101)"));
    }
    if (m_hasGoto) return;
    unreachableCheck(def->body);
    const Type* rt = fn->type && fn->type->kind == Type::Function ? fn->type->inner : 0;
    if (rt && rt->kind != Type::Void && rt->kind != Type::Error && rt->kind != Type::Dependent && !fn->isConstructor && !fn->isDestructor && fn->name != "main" && !fn->isNoreturn && mayFallThrough(def->body))
        warning(fn->loc, "S155", "'" + fn->name + "': not all control paths return a value (cl C4715)");
    m_uninitCandidates.clear();
    for (size_t i = 0; i < m_locals.size(); ++i) if (!m_locals[i]->init && !m_locals[i]->isParam && m_locals[i]->type && m_locals[i]->type->kind != Type::Array) m_uninitCandidates.insert(m_locals[i]);
    if (!m_uninitCandidates.empty()) { std::set<Symbol*> assigned; uninitStmt(def->body, assigned); }
}

bool Sema::isNoreturnCall(cxx::Expr* e)
{
    while (e && (e->kind == cxx::Expr::Paren || e->kind == cxx::Expr::Cast || e->kind == cxx::Expr::NamedCast) && e->kids.size() == 1) e = e->kids[0];
    return e && e->kind == cxx::Expr::Call && e->semaSym && e->semaSym->kind == Symbol::Function && e->semaSym->isNoreturn;
}

bool Sema::hasJump(cxx::Stmt* s, bool wantContinue)
{
    if (!s) return false;
    switch (s->kind) {
    case cxx::Stmt::Break: return !wantContinue;
    case cxx::Stmt::Continue: return wantContinue;
    case cxx::Stmt::While: case cxx::Stmt::For: case cxx::Stmt::DoWhile: return false;   // their own jumps
    case cxx::Stmt::Switch: if (!wantContinue) return false; break;                       // a break inside targets the switch
    default: break;
    }
    for (size_t i = 0; i < s->kids.size(); ++i) if (hasJump(s->kids[i], wantContinue)) return true;
    return false;
}

bool Sema::hasDefault(cxx::Stmt* s)
{
    if (!s) return false;
    if (s->kind == cxx::Stmt::Default) return true;
    if (s->kind == cxx::Stmt::Switch) return false;
    for (size_t i = 0; i < s->kids.size(); ++i) if (hasDefault(s->kids[i])) return true;
    return false;
}

// whether control can reach the end of the statement (a conservative flow analysis: an infinite loop ends
// only through a break, a switch with a default only through a break or a case that falls off; a label makes what
// follows reachable)
bool Sema::mayFallThrough(cxx::Stmt* s)
{
    if (!s) return true;
    switch (s->kind) {
    case cxx::Stmt::Compound: {
        bool reachable = true;
        for (size_t i = 0; i < s->kids.size(); ++i) {
            cxx::Stmt* k = s->kids[i];
            if (k->kind == cxx::Stmt::Label || k->kind == cxx::Stmt::Case || k->kind == cxx::Stmt::Default) reachable = true;
            if (reachable) reachable = mayFallThrough(k);
        }
        return reachable;
    }
    case cxx::Stmt::ExprStmt: return !(s->expr && (s->expr->kind == cxx::Expr::Throw || isNoreturnCall(s->expr)));
    case cxx::Stmt::Return: case cxx::Stmt::Goto: case cxx::Stmt::Break: case cxx::Stmt::Continue: return false;
    case cxx::Stmt::If: return mayFallThrough(s->kids.empty() ? 0 : s->kids[0]) || s->kids.size() < 2 || mayFallThrough(s->kids[1]);
    case cxx::Stmt::While: case cxx::Stmt::For: {
        const bool forever = (!s->expr && !s->decl) || (s->expr && s->expr->semaConst && s->expr->semaValue != 0);
        return !forever || hasJump(s->kids.empty() ? 0 : s->kids[0], false);
    }
    case cxx::Stmt::DoWhile: {
        cxx::Stmt* body = s->kids.empty() ? 0 : s->kids[0];
        const bool forever = s->expr && s->expr->semaConst && s->expr->semaValue != 0;
        if (hasJump(body, false)) return true;
        if (forever) return false;
        return mayFallThrough(body) || hasJump(body, true);
    }
    case cxx::Stmt::Switch: {
        cxx::Stmt* body = s->kids.empty() ? 0 : s->kids[0];
        if (!body || !hasDefault(body)) return true;
        return hasJump(body, false) || mayFallThrough(body);
    }
    case cxx::Stmt::Try: {
        bool ft = false;
        for (size_t i = 0; i < s->kids.size(); ++i) if (mayFallThrough(s->kids[i])) ft = true;
        return ft;
    }
    case cxx::Stmt::Handler: case cxx::Stmt::Label: case cxx::Stmt::Case: case cxx::Stmt::Default:
        return s->kids.empty() || mayFallThrough(s->kids[0]);
    default: return true;
    }
}

bool Sema::generatesCode(cxx::Stmt* s)
{
    if (!s) return false;
    switch (s->kind) {
    case cxx::Stmt::ExprStmt: return s->expr != 0;
    case cxx::Stmt::Return: case cxx::Stmt::If: case cxx::Stmt::While: case cxx::Stmt::DoWhile: case cxx::Stmt::For: case cxx::Stmt::Switch: case cxx::Stmt::Try: case cxx::Stmt::Goto: return true;
    case cxx::Stmt::Compound: for (size_t i = 0; i < s->kids.size(); ++i) if (generatesCode(s->kids[i])) return true; return false;
    case cxx::Stmt::DeclStmt: {
        if (!s->decl) return false;
        for (size_t i = 0; i < s->decl->declarators.size(); ++i) {
            const cxx::InitDeclarator& id = s->decl->declarators[i];
            if (id.init.kind != cxx::Initializer::None) return true;
            Symbol* v = id.decl ? id.decl->semaSym : 0;
            if (v && v->kind == Symbol::Variable && v->type && m_types.unref(v->type)->kind == Type::Class) return true;
        }
        return false;
    }
    default: return false;
    }
}

void Sema::unreachableCheck(cxx::Stmt* s)
{
    if (!s) return;
    if (s->kind == cxx::Stmt::Compound) {
        bool reachable = true, reported = false;
        for (size_t i = 0; i < s->kids.size(); ++i) {
            cxx::Stmt* k = s->kids[i];
            if (k->kind == cxx::Stmt::Label || k->kind == cxx::Stmt::Case || k->kind == cxx::Stmt::Default) reachable = true;
            if (!reachable && !reported && generatesCode(k)) { warning(k->loc, "S157", "unreachable code (cl C4702)"); reported = true; }
            if (reachable) reachable = mayFallThrough(k);
            if (k->kind == cxx::Stmt::Try) reachable = true;
            unreachableCheck(k);
        }
        return;
    }
    for (size_t i = 0; i < s->kids.size(); ++i) unreachableCheck(s->kids[i]);
    if (s->init) unreachableCheck(s->init);
}

// an uninitialised scalar local read before any statement that may assign it (textual order; the branches of an
// if, a loop body and the handlers of a try each start from the state before them, and what any of them assigns
// counts afterwards). A name passed to a non-const reference, whose address is taken, or assigned to is written
static cxx::Expr* unparen(cxx::Expr* e)
{
    while (e && e->kind == cxx::Expr::Paren && e->kids.size() == 1) e = e->kids[0];
    return e;
}

void Sema::uninitStmt(cxx::Stmt* s, std::set<Symbol*>& assigned)
{
    if (!s) return;
    switch (s->kind) {
    case cxx::Stmt::Compound: case cxx::Stmt::Label: case cxx::Stmt::Case: case cxx::Stmt::Default: case cxx::Stmt::Handler:
        for (size_t i = 0; i < s->kids.size(); ++i) uninitStmt(s->kids[i], assigned);
        break;
    case cxx::Stmt::ExprStmt: uninitExpr(s->expr, assigned, false); break;
    case cxx::Stmt::DeclStmt: uninitDecl(s->decl, assigned); break;
    case cxx::Stmt::Return: uninitExpr(s->expr, assigned, false); break;
    case cxx::Stmt::If: {
        if (s->decl) uninitDecl(s->decl, assigned); else uninitExpr(s->expr, assigned, false);
        std::set<Symbol*> a = assigned, b = assigned;
        if (s->kids.size() > 0) uninitStmt(s->kids[0], a);
        if (s->kids.size() > 1) uninitStmt(s->kids[1], b);
        assigned.insert(a.begin(), a.end()); assigned.insert(b.begin(), b.end());
        break;
    }
    case cxx::Stmt::While: case cxx::Stmt::Switch: case cxx::Stmt::DoWhile: case cxx::Stmt::For: {
        if (s->kind == cxx::Stmt::For && s->init) uninitStmt(s->init, assigned);
        if (s->kind != cxx::Stmt::DoWhile) { if (s->decl) uninitDecl(s->decl, assigned); else uninitExpr(s->expr, assigned, false); }
        std::set<Symbol*> a = assigned;
        for (size_t i = 0; i < s->kids.size(); ++i) uninitStmt(s->kids[i], a);
        if (s->kind == cxx::Stmt::DoWhile) uninitExpr(s->expr, a, false);
        if (s->kind == cxx::Stmt::For) uninitExpr(s->expr2, a, false);
        assigned.insert(a.begin(), a.end());
        break;
    }
    case cxx::Stmt::Try: {
        for (size_t i = 0; i < s->kids.size(); ++i) { std::set<Symbol*> a = assigned; uninitStmt(s->kids[i], a); assigned.insert(a.begin(), a.end()); }
        break;
    }
    default: break;
    }
}

void Sema::uninitDecl(cxx::Decl* d, std::set<Symbol*>& assigned)
{
    if (!d || d->kind != cxx::Decl::Simple) return;
    for (size_t i = 0; i < d->declarators.size(); ++i) {
        const cxx::InitDeclarator& id = d->declarators[i];
        Symbol* v = id.decl ? id.decl->semaSym : 0;
        const Type* vt = v && v->kind == Symbol::Variable ? v->type : 0;
        for (size_t a = 0; a < id.init.args.size(); ++a) {
            cxx::Expr* x = id.init.args[a];
            // a reference bound to the local, or a constructor taking it by non-const reference, writes it
            const Type* pt = 0;
            if (vt && vt->kind == Type::Reference && id.init.args.size() == 1) pt = vt;
            else if (v && v->initCtor && a < v->initCtor->params.size()) pt = v->initCtor->params[a]->type;
            const bool write = x && unparen(x)->kind == cxx::Expr::Name && pt && pt->kind == Type::Reference && !pt->inner->isConst;
            uninitExpr(x, assigned, write);
        }
    }
}

void Sema::uninitExpr(cxx::Expr* e, std::set<Symbol*>& assigned, bool write)
{
    if (!e) return;
    if (e->kind == cxx::Expr::Paren && e->kids.size() == 1) { uninitExpr(e->kids[0], assigned, write); return; }
    switch (e->kind) {
    case cxx::Expr::Name: {
        Symbol* v = e->semaSym;
        if (!v || v->kind != Symbol::Variable || !m_uninitCandidates.count(v)) return;
        if (write) { assigned.insert(v); return; }
        if (!assigned.count(v)) { warning(e->loc, "S156", "uninitialized local variable '" + v->name + "' used (cl C4700)"); assigned.insert(v); }
        return;
    }
    case cxx::Expr::Assign:
        if (e->kids.size() < 2) return;
        uninitExpr(e->kids[1], assigned, false);
        if (e->text != "=") uninitExpr(e->kids[0], assigned, false);
        if (unparen(e->kids[0])->kind == cxx::Expr::Name) uninitExpr(e->kids[0], assigned, true); else uninitExpr(e->kids[0], assigned, false);
        return;
    case cxx::Expr::Unary:
        if (e->kids.empty()) return;
        if (e->text == "&" && unparen(e->kids[0])->kind == cxx::Expr::Name) { uninitExpr(e->kids[0], assigned, true); return; }
        uninitExpr(e->kids[0], assigned, false);
        return;
    case cxx::Expr::Call: {
        if (e->kids.empty()) return;
        uninitExpr(e->kids[0], assigned, false);
        Symbol* f = e->semaSym && e->semaSym->kind == Symbol::Function ? e->semaSym : 0;
        const Type* ft = f && f->type && f->type->kind == Type::Function ? f->type : 0;
        for (size_t a = 1; a < e->kids.size(); ++a) {
            cxx::Expr* x = e->kids[a];
            const Type* pt = ft && a - 1 < ft->params.size() ? ft->params[a - 1] : 0;
            bool w = false;
            if (unparen(x)->kind == cxx::Expr::Name) w = !ft ? true : (pt && pt->kind == Type::Reference && !pt->inner->isConst);
            uninitExpr(x, assigned, w);
        }
        return;
    }
    case cxx::Expr::SizeofExpr: case cxx::Expr::SizeofType: case cxx::Expr::Typeid: return;
    default:
        if (e->semaSym && e->semaSym->kind == Symbol::Function && e->semaSym->type && e->semaSym->type->kind == Type::Function
            && (e->kind == cxx::Expr::Binary || e->kind == cxx::Expr::Unary || e->kind == cxx::Expr::PostIncDec || e->kind == cxx::Expr::Index)) {
            Symbol* f = e->semaSym;
            const size_t first = f->ownerClass && !f->isStatic ? 1 : 0;   // a member operator: kids[0] is the object
            for (size_t k = 0; k < e->kids.size(); ++k) {
                const Type* pt = k >= first && k - first < f->type->params.size() ? f->type->params[k - first] : 0;
                const bool w = unparen(e->kids[k])->kind == cxx::Expr::Name && pt && pt->kind == Type::Reference && !pt->inner->isConst;
                uninitExpr(e->kids[k], assigned, w);
            }
            return;
        }
        for (size_t i = 0; i < e->kids.size(); ++i) uninitExpr(e->kids[i], assigned, false);
        for (size_t i = 0; i < e->placement.size(); ++i) uninitExpr(e->placement[i], assigned, false);
        return;
    }
}

} // namespace sema
} // namespace cppparse
