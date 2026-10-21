// IrGen.cpp - version 6 stage 4: IR generation from the analysed syntax tree. See IrGen.hpp.
#include "IrGen.hpp"
#include "Literal.hpp"
#include "Mangle.hpp"
#include "PathUtil.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace cppparse {

using sema::Symbol;
using sema::Scope;

namespace {

std::string numText(long v) { return intToString(static_cast<int>(v)); }

bool isReference(const sema::Type* t) { return t && t->kind == sema::Type::Reference; }

// a non-static data member or member function: what &C::m makes a pointer to member of
bool isNonStaticMember(const sema::Symbol* s)
{
    if (!s || s->isStatic) return false;
    if (s->kind == sema::Symbol::Variable) return s->isMember;
    return s->kind == sema::Symbol::Function && s->ownerClass && !s->isConstructor && !s->isDestructor;
}

// internal linkage: a static function or variable at namespace scope, a namespace-scope object declared const
// and not extern (3.5/3: `const int k = 1;` in two units is two objects), or anything in an anonymous namespace
bool hasInternalLinkage(const sema::Symbol* s)
{
    if (!s) return false;
    if (s->isStatic && !s->ownerClass && !s->isLocal) return true;
    if (s->kind == sema::Symbol::Variable && !s->isMember && !s->isLocal && !s->isParam && !s->isExtern && s->type && s->type->kind != sema::Type::Reference && s->type->isConst && !s->type->isVolatile) return true;
    for (const sema::Scope* sc = s->scope; sc; sc = sc->parent)
        if (sc->kind == sema::Scope::Namespace && sc->owner && sc->owner->name.empty()) return true;
    return false;
}

// cl's name of the vector deleting destructor from the destructor's (or its thunk's): ??1D@@UEAA@XZ becomes
// ??_ED@@UEAAPEAXI@Z, void* (unsigned int); the adjustor thunk ??_ED@@W7EAAPEAXI@Z
std::string deletingName(const std::string& dtor)
{
    if (dtor.size() < 6 || dtor.compare(0, 3, "??1") != 0 || dtor.compare(dtor.size() - 3, 3, "@XZ") != 0) return dtor + "_E";
    return "??_E" + dtor.substr(3, dtor.size() - 6) + "PEAXI@Z";
}

// appends n zero bytes to static data, extending a zero run that ends it
void zeroData(std::vector<IrDataItem>& out, int n)
{
    if (n <= 0) return;
    if (!out.empty() && out.back().kind == IrDataItem::Zero) { out.back().size += n; return; }
    IrDataItem z;
    z.size = n;
    out.push_back(z);
}

// x without its parentheses when it is a cast (no conversion function) of a floating operand, else 0
cxx::Expr* floatingCast(cxx::Expr* x)
{
    while (x && x->kind == cxx::Expr::Paren && x->kids.size() == 1) x = x->kids[0];
    if (!x || (x->kind != cxx::Expr::Cast && x->kind != cxx::Expr::NamedCast && x->kind != cxx::Expr::FunctionalCast) || x->kids.size() != 1 || x->semaSym) return 0;
    if (x->kind == cxx::Expr::NamedCast && x->text != "static_cast") return 0;
    const sema::Type* from = x->kids[0]->semaType;
    if (from && from->kind == sema::Type::Reference) from = from->inner;
    return from && from->isFloating() ? x : 0;
}

} // namespace

static Symbol* namedLocal(cxx::Expr* e);

IrGen::IrGen(sema::Program& program, sema::TypeTable& semaTypes)
    : m_prog(program), m_semaTypes(semaTypes), m_types(m_emptyCb), m_outList(0), m_generated(0), m_out(0), m_fn(0), m_block(0), m_sretSlot(-1), m_retReg(0), m_nrvoEnabled(false), m_nrvo(0), m_scopeSerial(0), m_tryCount(0),
      m_initSymbol(0), m_exitThunksDone(0), m_x86(0), m_msvcNames(false), m_deletingDone(0), m_ctorFlag(1), m_vbFlagSlot(-1),m_staticCount(0), m_guardCount(0), m_inPad(0), m_unwindId(-1), m_bodyBlock(-1),
      m_functionTryDef(0), m_rawTemp(-1), m_rawFunction(0), m_rawBlock(-1), m_rawEnd(0), m_raw(0), m_thisSlot(0) {}

IrGen::~IrGen()
{
    for (size_t i = 0; i < m_ownedClassInfos.size(); ++i) delete m_ownedClassInfos[i];
}

std::string IrGen::irName(const Symbol* fn)
{
    if (fn->isExternC || (fn->name == "main" && !fn->ownerClass && fn->scope && fn->scope->kind == Scope::Global)) return fn->name;
    std::string s = fn->qualifiedName() + "(";
    if (fn->type && fn->type->kind == sema::Type::Function) {
        for (size_t i = 0; i < fn->type->params.size(); ++i) s += (i ? ", " : "") + fn->type->params[i]->text();
        if (fn->type->variadic) s += fn->type->params.empty() ? "..." : ", ...";
    }
    s += ")";
    if (fn->type && fn->type->constMethod) s += " const";
    return s;
}

const ClassInfo* IrGen::classInfo(Symbol* cls)
{
    std::map<Symbol*, ClassInfo*>::iterator it = m_classInfos.find(cls);
    if (it != m_classInfos.end()) return it->second;
    ClassInfo* c = new ClassInfo;
    m_ownedClassInfos.push_back(c);
    m_classInfos[cls] = c;
    c->name = cls->name;
    c->qualifiedName = cls->qualifiedName();
    c->kind = cls->isUnion ? ClassInfo::Union : cls->isStruct ? ClassInfo::Struct : ClassInfo::Class;
    c->isDefinition = cls->isDefinition;
    for (size_t b = 0; b < cls->bases.size(); ++b) {
        BaseClass bc;
        bc.name = cls->bases[b].cls ? cls->bases[b].cls->qualifiedName() : "";
        bc.resolved = cls->bases[b].cls ? const_cast<ClassInfo*>(classInfo(cls->bases[b].cls)) : 0;
        c->bases.push_back(bc);
    }
    return c;
}

const Type* IrGen::oldType(const sema::Type* t)
{
    if (!t) return m_types.builtin(Type::Long);
    std::map<const sema::Type*, const Type*>::iterator it = m_typeCache.find(t);
    if (it != m_typeCache.end()) return it->second;
    const Type* r;
    switch (t->kind) {
    case sema::Type::Void: r = m_types.builtin(Type::Void, false, t->isConst); break;
    case sema::Type::Bool: r = m_types.builtin(Type::Bool, false, t->isConst); break;
    case sema::Type::Char: case sema::Type::SChar: r = m_types.builtin(Type::Char, false, t->isConst); break;
    case sema::Type::UChar: r = m_types.builtin(Type::Char, true, t->isConst); break;
    case sema::Type::WChar: case sema::Type::UShort: r = m_types.builtin(Type::Short, true, t->isConst); break;
    case sema::Type::Short: r = m_types.builtin(Type::Short, false, t->isConst); break;
    case sema::Type::Int: r = m_types.builtin(Type::Int, false, t->isConst); break;
    case sema::Type::UInt: r = m_types.builtin(Type::Int, true, t->isConst); break;
    case sema::Type::Long: r = m_types.builtin(Type::Long, false, t->isConst); break;
    case sema::Type::ULong: r = m_types.builtin(Type::Long, true, t->isConst); break;
    case sema::Type::LongLong: r = m_types.builtin(Type::LongLong, false, t->isConst); break;
    case sema::Type::ULongLong: r = m_types.builtin(Type::LongLong, true, t->isConst); break;
    case sema::Type::Float: r = m_types.builtin(Type::Float, false, t->isConst); break;
    case sema::Type::Double: case sema::Type::LongDouble: r = m_types.builtin(Type::Double, false, t->isConst); break;
    case sema::Type::NullPtr: r = m_types.nullType(); break;
    case sema::Type::Pointer: r = m_types.pointer(oldType(t->inner), t->isConst); break;
    case sema::Type::MemberPointer: {
        // cl's representations (mpLayout): an int offset, a code pointer, or an aggregate in memory
        const int k = mpKind(t);
        if (k == 1) r = m_types.builtin(Type::Int);
        else if (k == 2) r = m_types.pointer(m_types.function(oldType(t->inner ? t->inner->inner : 0)));
        else r = mpClassType(m_semaTypes.sizeOf(t));
        break;
    }
    case sema::Type::Reference: r = m_types.reference(oldType(t->inner)); break;
    case sema::Type::Array: r = m_types.array(oldType(t->inner), t->arraySize > 0 ? static_cast<int>(t->arraySize) : 0); break;
    case sema::Type::Function: r = m_types.function(oldType(t->inner)); break;
    case sema::Type::Class: r = m_types.classType(t->decl ? classInfo(t->decl) : 0, t->isConst); break;
    case sema::Type::Enum: r = m_types.enumType(t->decl ? t->decl->qualifiedName() : std::string("enum"), t->isConst); break;
    default: r = m_types.unknown(t->text()); break;
    }
    m_typeCache[t] = r;
    return r;
}

namespace {

// the class a qualified member access names ("d.C::a": C), searched by name among the bases of the object's
// class; 0 when unqualified or not found. A base held more than once must be reached through it (sema S144
// refuses the ambiguous forms)
Symbol* namingBase(const cxx::Expr* member, Symbol* cls, int depth = 0)
{
    if (!member || !cls || member->name.parts.size() < 2 || depth > 32) return 0;
    const std::string& q = member->name.parts[member->name.parts.size() - 2].ident;
    for (size_t i = 0; i < cls->bases.size(); ++i) {
        Symbol* b = cls->bases[i].cls;
        if (b && b->name == q) return b;
    }
    for (size_t i = 0; i < cls->bases.size(); ++i) { Symbol* r = namingBase(member, cls->bases[i].cls, depth + 1); if (r) return r; }
    return 0;
}

} // namespace

bool IrGen::baseOffset(Symbol* derived, Symbol* base, int& offset, int depth) const
{
    if (!derived || !base || depth > 32) return false;
    if (derived == base) { offset = 0; return true; }
    for (size_t i = 0; i < derived->bases.size(); ++i) {
        int o = 0;
        if (derived->bases[i].isVirtual) continue;   // its offset holds only in a complete object: virtualBasePath
        if (baseOffset(derived->bases[i].cls, base, o, depth + 1)) { offset = derived->bases[i].offset + o; return true; }
    }
    return false;
}

bool IrGen::virtualBasePath(Symbol* derived, Symbol* base, int& index, int& rest) const
{
    if (!derived || !base || derived->vbases.empty()) return false;
    const sema::VbTable* table = 0;
    for (size_t t = 0; t < derived->vbtables.size(); ++t) if (derived->vbtables[t].offset == derived->vbptrOffset) table = &derived->vbtables[t];
    if (!table) return false;
    for (size_t k = 0; k < table->order.size(); ++k) {
        int o = 0;
        if (baseOffset(table->order[k], base, o)) { index = static_cast<int>(k) + 1; rest = o; return true; }
    }
    return false;
}

void IrGen::unsupported(int line, const std::string& what)
{
    const std::string m = "line " + numText(static_cast<long>(line)) + ": " + what;
    if (std::find(m_unsupported.begin(), m_unsupported.end(), m) == m_unsupported.end()) m_unsupported.push_back(m);   // once per line
}

// the labels defined inside statement s
void IrGen::collectLabels(const cxx::Stmt* s, std::set<std::string>& out)
{
    if (!s) return;
    if (s->kind == cxx::Stmt::Label) out.insert(s->label);
    for (size_t i = 0; i < s->kids.size(); ++i) collectLabels(s->kids[i], out);
}

// MSVC's conversion to a virtual base: the vbptr's table holds the base's distance from the vbptr
int IrGen::toBase(int addr, Symbol* derived, Symbol* base, int line)
{
    const sema::Type* dp = m_semaTypes.pointer(m_semaTypes.classType(derived));
    const sema::Type* bp = m_semaTypes.pointer(m_semaTypes.classType(base));
    int off = 0, index = 0, rest = 0;
    if (baseOffset(derived, base, off)) return off ? offsetAddress(addr, dp, bp, off, line) : addr;
    if (!virtualBasePath(derived, base, index, rest)) { unsupported(line, "internal error: no path from '" + derived->qualifiedName() + "' to its base '" + base->qualifiedName() + "'"); return addr; }
    const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
    const sema::Type* ip = m_semaTypes.pointer(it);
    const sema::Type* cp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Char));
    int vbptr = offsetAddress(addr, dp, cp, derived->vbptrOffset, line);
    LValue pl; pl.kind = LValue::Mem; pl.addr = vbptr; pl.type = ip;
    int table = load(pl, ip, line);
    int entry = offsetAddress(table, ip, ip, 4L * index, line);
    LValue el; el.kind = LValue::Mem; el.addr = entry; el.type = it;
    int disp = convert(load(el, it, line), it, m_semaTypes.basic(sema::Type::LongLong), line);
    IrInst in = make(IrInst::AddrIndex, bp, line);
    in.extra = 1;
    in.args.push_back(IrOperand::temp(vbptr, oldType(cp)));
    in.args.push_back(IrOperand::temp(disp, oldType(m_semaTypes.basic(sema::Type::LongLong))));
    int r = emit(in);
    return rest ? offsetAddress(r, bp, bp, rest, line) : r;
}

// ---- pointers to members (cl's representations, see MpLayout) -------------------------------------------

IrGen::MpLayout IrGen::mpLayout(const sema::Type* t) const
{
    MpLayout l;
    t = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (!t || t->kind != sema::Type::MemberPointer) return l;
    Symbol* mc = t->memberClass && t->memberClass->kind == sema::Type::Class ? t->memberClass->decl : 0;
    l.function = t->inner && t->inner->kind == sema::Type::Function;
    l.unspecified = !mc || !mc->isDefinition || mc->memberPointerUnspecified;
    if (mc) sema::TypeTable::memberPointerModel(mc, l.multiple, l.virt, 0);
    l.size = m_semaTypes.sizeOf(t);
    if (l.function) {
        l.adjAt = l.multiple || l.virt || l.unspecified ? 8 : -1;
        l.vbptrAt = l.unspecified ? 12 : -1;
        l.vbindexAt = l.unspecified ? 16 : l.virt ? 12 : -1;
    } else {
        l.vbptrAt = l.unspecified ? 4 : -1;
        l.vbindexAt = l.unspecified ? 8 : l.virt ? 4 : -1;
    }
    return l;
}

// 0: not a pointer to member; 1: an int offset; 2: a code pointer; 3: an aggregate in memory
int IrGen::mpKind(const sema::Type* t) const
{
    t = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (!t || t->kind != sema::Type::MemberPointer) return 0;
    const MpLayout l = mpLayout(t);
    if (l.adjAt >= 0 || l.vbindexAt >= 0) return 3;
    return l.function ? 2 : 1;
}

// the old type of an aggregate representation: a class of its size, so that the back end keeps its values in memory
const Type* IrGen::mpClassType(int size)
{
    std::map<int, const Type*>::const_iterator it = m_mpTypes.find(size);
    if (it != m_mpTypes.end()) return it->second;
    ClassInfo* c = new ClassInfo;
    m_ownedClassInfos.push_back(c);
    c->name = c->qualifiedName = "`member pointer'{" + numText(size) + "}";
    c->kind = ClassInfo::Struct;
    c->isDefinition = true;
    const Type* t = m_types.classType(c);
    m_mpTypes[size] = t;
    return t;
}

int IrGen::mpLoad(int addr, int at, const sema::Type* ft, int line)
{
    IrInst fa = make(IrInst::AddrField, m_semaTypes.pointer(ft), line);
    fa.sub = "mp"; fa.extra = at;
    fa.args.push_back(IrOperand::temp(addr, oldType(m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void)))));
    LValue m; m.kind = LValue::Mem; m.addr = emit(fa); m.type = ft;
    return load(m, ft, line);
}

void IrGen::mpStore(int addr, int at, int v, const sema::Type* ft, int line)
{
    IrInst fa = make(IrInst::AddrField, m_semaTypes.pointer(ft), line);
    fa.sub = "mp"; fa.extra = at;
    fa.args.push_back(IrOperand::temp(addr, oldType(m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void)))));
    LValue m; m.kind = LValue::Mem; m.addr = emit(fa); m.type = ft;
    store(m, v, ft, line);
}

// the address of a new zeroed slot for an aggregate representation
int IrGen::mpTemp(const sema::Type* t, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    const int slot = newLocal("tmp.mp", u, true);
    LValue tl; tl.kind = LValue::Local; tl.local = slot; tl.type = u;
    const int addr = addressOf(tl, line);
    zeroBytes(addr, sizeOf(u), line);
    return addr;
}

// the null value of a pointer to member type
int IrGen::mpNull(const sema::Type* t, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
    const MpLayout l = mpLayout(u);
    const int k = mpKind(u);
    if (k == 1) return constant(-1, it, line);
    if (k == 2) return constant(0, u, line);
    const int r = mpTemp(u, line);
    if (l.vbindexAt >= 0 && (!l.function || l.unspecified)) mpStore(r, l.vbindexAt, constant(-1, it, line), it, line);
    return r;
}

// where offsets of a class's own members count from under its virtual-inheritance model: the vbptr's subobject,
// which its vbtable's entry 0 leads back to (cl: object + vbptr offset + vbtable[0] + offset)
int IrGen::vbptrOrigin(const Symbol* cls)
{
    if (!cls || cls->vbptrOffset < 0) return 0;
    for (size_t t = 0; t < cls->vbtables.size(); ++t)
        if (cls->vbtables[t].offset == cls->vbptrOffset && !cls->vbtables[t].entries.empty()) return cls->vbptrOffset + cls->vbtables[t].entries[0];
    return 0;
}

// the path from derived down to base: a non-virtual offset (index 0), or through the virtual base at entry index of
// derived's own vbtable, rest bytes further
bool IrGen::mpPath(Symbol* derived, Symbol* base, int& off, int& index, int& rest) const
{
    off = index = rest = 0;
    if (!derived || !base) return false;
    if (baseOffset(derived, base, off)) return true;
    return virtualBasePath(derived, base, index, rest);
}

// the vcall thunk of a virtual function (??_9P@@$BA@AA: this is the vfptr its slot belongs to, so a function first held
// by a secondary vftable is reached with this moved there, as cl stores the move in the pointer's adjustment)
std::string IrGen::vcallThunk(Symbol* fn)
{
    const int slotOffset = 8 * (fn->vtableSlot < 0 ? 0 : fn->vtableSlot);
    const std::string name = fn->ownerClass->qualifiedName() + "::`vcall'{" + numText(slotOffset) + "}()";
    if (m_x86) {
        m_x86->addVcallThunk(name, slotOffset);
        if (m_msvcNames) m_x86->registerSymbolName(name, msvcMangleVcallThunk(*fn->ownerClass, slotOffset));
    }
    return name;
}

// &C::m as a value of the pointer to member type t (the member's own class's)
int IrGen::mpAddressOf(Symbol* m, const sema::Type* t, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
    const MpLayout l = mpLayout(u);
    const int k = mpKind(u);
    Symbol* mc = u->memberClass && u->memberClass->kind == sema::Type::Class ? u->memberClass->decl : 0;
    if (!l.function) {
        int off = m->offset;
        if (l.virt && !l.unspecified) off -= vbptrOrigin(mc);
        if (k == 1) return constant(off, it, line);
        const int r = mpTemp(u, line);
        mpStore(r, 0, constant(off, it, line), it, line);
        return r;
    }
    need(m);
    const std::string target = m->isVirtual ? vcallThunk(m) : irName(m);
    IrInst fa = make(IrInst::AddrGlobal, 0, line);
    fa.type = m_types.function(oldType(m->type ? m->type->inner : 0));
    fa.args.push_back(IrOperand::global(target, fa.type));
    const int ptr = emit(fa);
    if (k == 2) return ptr;
    const sema::Type* pf = m_semaTypes.pointer(m->type);
    const int r = mpTemp(u, line);
    mpStore(r, 0, ptr, pf, line);
    if (l.adjAt >= 0 && m->thisAdjust) mpStore(r, l.adjAt, constant(m->thisAdjust, it, line), it, line);
    return r;
}

// a pointer to member converted between the types of a class and its base (4.11; a static_cast the other way):
// cl adds the base's offset (data: unless null; a virtual base: the member's offset in it and the base's vbtable
// entry), and keeps a value of a class's own virtual model when the derived class shares its vbptr
int IrGen::mpConvert(int v, const sema::Type* from, const sema::Type* to, int line)
{
    const sema::Type* fu = m_semaTypes.unqualified(m_semaTypes.unref(from));
    const sema::Type* tu = m_semaTypes.unqualified(m_semaTypes.unref(to));
    const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
    Symbol* fc = fu->memberClass && fu->memberClass->kind == sema::Type::Class ? fu->memberClass->decl : 0;
    Symbol* tc = tu->memberClass && tu->memberClass->kind == sema::Type::Class ? tu->memberClass->decl : 0;
    if (!fc || !tc || fc == tc) return v;
    const MpLayout lf = mpLayout(fu), lt = mpLayout(tu);
    const int kf = mpKind(fu), kt = mpKind(tu);
    const std::string what = "conversion of a pointer to member from '" + fu->text() + "' to '" + tu->text() + "'";
    int off = 0, index = 0, rest = 0;
    bool up = true;
    if (!mpPath(tc, fc, off, index, rest)) {
        up = false;
        if (!mpPath(fc, tc, off, index, rest) || index) { unsupported(line, what + " is not supported by --compile6 (no non-virtual path between the classes)"); return v; }
    }
    if (lt.unspecified) {
        // into the unspecified model (cl's general one): the offset from the object, or the offset in the virtual
        // base with the vbptr's offset and the base's vbtable entry; a null data member pointer stays null
        if (lf.unspecified || !up) { unsupported(line, what + ", whose inheritance model is unspecified, is not supported yet by --compile6"); return v; }
        const int r = mpTemp(tu, line);
        const int delta = index ? rest : off;
        int nb = -1, jb = -1;
        if (!lf.function) {
            const int nl = m_out->newBlock("mp.null");
            nb = m_out->newBlock("mp.nonnull"); jb = m_out->newBlock("mp.end");
            condbr(binary("!=", v, it, constant(-1, it, line), it, m_semaTypes.basic(sema::Type::Bool), line), nb, nl, line);
            m_block = nl;
            mpStore(r, lt.vbindexAt, constant(-1, it, line), it, line);
            br(jb, line);
            m_block = nb;
            mpStore(r, 0, delta ? binary("+", v, it, constant(delta, it, line), it, it, line) : v, it, line);
        } else {
            const sema::Type* pf = m_semaTypes.pointer(fu->inner);
            mpStore(r, 0, kf == 2 ? v : mpLoad(v, 0, pf, line), pf, line);
            int adj = kf == 3 && lf.adjAt >= 0 ? mpLoad(v, lf.adjAt, it, line) : -1;
            if (adj < 0) adj = constant(delta, it, line);
            else if (delta) adj = binary("+", adj, it, constant(delta, it, line), it, it, line);
            mpStore(r, lt.adjAt, adj, it, line);
        }
        if (index) {
            mpStore(r, lt.vbptrAt, constant(tc->vbptrOffset, it, line), it, line);
            mpStore(r, lt.vbindexAt, constant(4 * index, it, line), it, line);
        }
        if (jb >= 0) { br(jb, line); m_block = jb; }
        return r;
    }
    if (lf.unspecified) {
        // out of the unspecified model, from a class without virtual bases (its vbtable field is 0, or -1 for null)
        if (!up || hasVbases(fc) || (lt.virt && !lf.function)) { unsupported(line, what + ", whose inheritance model is unspecified, is not supported yet by --compile6"); return v; }
        if (!lf.function) {
            const int slot = newLocal("tmp.mp", it, false);
            LValue sl; sl.kind = LValue::Local; sl.local = slot; sl.type = it;
            const int nb = m_out->newBlock("mp.nonnull"), jb = m_out->newBlock("mp.end");
            store(sl, constant(-1, it, line), it, line);
            condbr(binary("!=", mpLoad(v, lf.vbindexAt, it, line), it, constant(-1, it, line), it, m_semaTypes.basic(sema::Type::Bool), line), nb, jb, line);
            m_block = nb;
            const int o = mpLoad(v, 0, it, line);
            store(sl, off ? binary("+", o, it, constant(off, it, line), it, it, line) : o, it, line);
            br(jb, line);
            m_block = jb;
            return load(sl, it, line);
        }
        const sema::Type* pf = m_semaTypes.pointer(fu->inner);
        const int ptr = mpLoad(v, 0, pf, line);
        if (kt == 2) return ptr;
        const int r = mpTemp(tu, line);
        mpStore(r, 0, ptr, pf, line);
        const int adj = mpLoad(v, lf.adjAt, it, line);
        mpStore(r, lt.adjAt, off ? binary("+", adj, it, constant(off, it, line), it, it, line) : adj, it, line);
        return r;
    }
    if (!lf.function) {
        if (kf == 1 && kt == 1) {
            if (off == 0) return v;
            // null stays null
            const int slot = newLocal("tmp.mp", it, false);
            LValue sl; sl.kind = LValue::Local; sl.local = slot; sl.type = it;
            const int nb = m_out->newBlock("mp.nonnull"), jb = m_out->newBlock("mp.end");
            store(sl, constant(-1, it, line), it, line);
            condbr(binary("!=", v, it, constant(-1, it, line), it, m_semaTypes.basic(sema::Type::Bool), line), nb, jb, line);
            m_block = nb;
            store(sl, binary(up ? "+" : "-", v, it, constant(off, it, line), it, it, line), it, line);
            br(jb, line);
            m_block = jb;
            return load(sl, it, line);
        }
        if (kf == 1 && kt == 3 && up) {
            const int r = mpTemp(tu, line);
            const int nb = m_out->newBlock("mp.nonnull"), nl = m_out->newBlock("mp.null"), jb = m_out->newBlock("mp.end");
            condbr(binary("!=", v, it, constant(-1, it, line), it, m_semaTypes.basic(sema::Type::Bool), line), nb, nl, line);
            m_block = nl;
            mpStore(r, lt.vbindexAt, constant(-1, it, line), it, line);
            br(jb, line);
            m_block = nb;
            const int delta = index ? rest : off - vbptrOrigin(tc);
            mpStore(r, 0, delta ? binary("+", v, it, constant(delta, it, line), it, it, line) : v, it, line);
            if (index) mpStore(r, lt.vbindexAt, constant(4 * index, it, line), it, line);
            br(jb, line);
            m_block = jb;
            return r;
        }
        if (kf == 3 && kt == 3 && up && !index && tc->vbptrOffset == off + fc->vbptrOffset) return v;   // the shared vbptr: cl copies the value
        unsupported(line, what + " is not supported yet by --compile6");
        return v;
    }
    const sema::Type* pf = m_semaTypes.pointer(fu->inner);
    const int ptr = kf == 2 ? v : mpLoad(v, 0, pf, line);
    if (kt == 2) return ptr;   // down to a single-inheritance class: the adjustment is dropped, as cl
    if (!up) { unsupported(line, what + " is not supported yet by --compile6"); return v; }
    if (kf == 3 && lf.virt && lt.virt) {
        if (!index && tc->vbptrOffset == off + fc->vbptrOffset) return v;
        unsupported(line, what + " is not supported yet by --compile6");
        return v;
    }
    const int r = mpTemp(tu, line);
    mpStore(r, 0, ptr, pf, line);
    int adj = kf == 3 && lf.adjAt >= 0 ? mpLoad(v, lf.adjAt, it, line) : -1;
    const int delta = index ? rest : (lt.virt ? off - vbptrOrigin(tc) : off);
    if (adj < 0) adj = constant(delta, it, line);
    else if (delta) adj = binary("+", adj, it, constant(delta, it, line), it, it, line);
    mpStore(r, lt.adjAt, adj, it, line);
    if (lt.vbindexAt >= 0 && index) mpStore(r, lt.vbindexAt, constant(4 * index, it, line), it, line);
    return r;
}

// a == b (a != b) on pointers to members, converted to the more derived class's type first: cl compares every field
// of a data member pointer, and of a function's the code pointer, then the rest unless it is null
int IrGen::mpCompare(int a, const sema::Type* ta, int b, const sema::Type* tb, bool ne, int line)
{
    ta = m_semaTypes.unqualified(m_semaTypes.decay(ta)); tb = m_semaTypes.unqualified(m_semaTypes.decay(tb));
    const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
    const sema::Type* bt = m_semaTypes.basic(sema::Type::Bool);
    if (ta->kind != sema::Type::MemberPointer) { std::swap(a, b); std::swap(ta, tb); }
    if (tb->kind != sema::Type::MemberPointer) { b = mpNull(ta, line); tb = ta; }
    else if (tb != ta) {
        Symbol* ac = ta->memberClass && ta->memberClass->kind == sema::Type::Class ? ta->memberClass->decl : 0;
        Symbol* bc = tb->memberClass && tb->memberClass->kind == sema::Type::Class ? tb->memberClass->decl : 0;
        if (ac && bc && isBaseOf(bc, ac)) { a = mpConvert(a, ta, tb, line); ta = tb; }
        else { b = mpConvert(b, tb, ta, line); tb = ta; }
    }
    const MpLayout l = mpLayout(ta);
    const int k = mpKind(ta);
    if (k == 1) return binary(ne ? "!=" : "==", a, it, b, it, bt, line);
    if (k == 2) {
        IrInst c = make(IrInst::Cmp, bt, line);
        c.sub = ne ? "ne" : "eq";
        c.args.push_back(IrOperand::temp(a, oldType(ta)));
        c.args.push_back(IrOperand::temp(b, oldType(ta)));
        return emit(c);
    }
    const int slot = newLocal("tmp.mpeq", bt, false);
    LValue sl; sl.kind = LValue::Local; sl.local = slot; sl.type = bt;
    store(sl, constant(ne ? 1 : 0, bt, line), bt, line);
    const int eqb = m_out->newBlock("mp.equal"), jb = m_out->newBlock("mp.end");
    std::vector<int> fields;
    if (!l.function) fields.push_back(0);
    if (l.adjAt >= 0) fields.push_back(l.adjAt);
    if (l.vbptrAt >= 0) fields.push_back(l.vbptrAt);
    if (l.vbindexAt >= 0) fields.push_back(l.vbindexAt);
    if (l.function) {
        const sema::Type* pf = m_semaTypes.pointer(ta->inner);
        const int pa = mpLoad(a, 0, pf, line);
        const int pb = mpLoad(b, 0, pf, line);
        IrInst c = make(IrInst::Cmp, bt, line);
        c.sub = "eq";
        c.args.push_back(IrOperand::temp(pa, oldType(pf)));
        c.args.push_back(IrOperand::temp(pb, oldType(pf)));
        const int rb = m_out->newBlock("mp.rest");
        condbr(emit(c), rb, jb, line);
        m_block = rb;
        // null pointers are equal whatever the other fields hold
        const int fb = m_out->newBlock("mp.fields");
        condbr(toBool(mpLoad(a, 0, pf, line), pf, line), fb, eqb, line);
        m_block = fb;
    }
    for (size_t f = 0; f < fields.size(); ++f) {
        const int fa = mpLoad(a, fields[f], it, line);
        const int fb = mpLoad(b, fields[f], it, line);
        const int next = f + 1 < fields.size() ? m_out->newBlock("mp.field") : eqb;
        condbr(binary("==", fa, it, fb, it, bt, line), next, jb, line);
        m_block = next;
    }
    if (fields.empty()) br(eqb, line);
    m_block = eqb;
    store(sl, constant(ne ? 0 : 1, bt, line), bt, line);
    br(jb, line);
    m_block = jb;
    return load(sl, bt, line);
}

// a pointer to member as a condition: not null (cl: a data member pointer differs from its null in some field, a
// function's code pointer is not 0)
int IrGen::mpToBool(int v, const sema::Type* t, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
    const sema::Type* bt = m_semaTypes.basic(sema::Type::Bool);
    const MpLayout l = mpLayout(u);
    const int k = mpKind(u);
    if (k == 1) return binary("!=", v, it, constant(-1, it, line), it, bt, line);
    if (k == 2 || l.function) {
        const sema::Type* pf = m_semaTypes.pointer(u->inner);
        const int ptr = k == 2 ? v : mpLoad(v, 0, pf, line);
        IrInst c = make(IrInst::Cmp, bt, line);
        c.sub = "ne";
        c.args.push_back(IrOperand::temp(ptr, oldType(pf)));
        c.args.push_back(IrOperand::imm(0, oldType(pf)));
        return emit(c);
    }
    const int zeroOff = binary("!=", mpLoad(v, 0, it, line), it, constant(0, it, line), it, bt, line);
    const int nullIndex = binary("!=", mpLoad(v, l.vbindexAt, it, line), it, constant(-1, it, line), it, bt, line);
    int r = binary("|", zeroOff, bt, nullIndex, bt, bt, line);
    if (l.vbptrAt >= 0) r = binary("|", r, bt, binary("!=", mpLoad(v, l.vbptrAt, it, line), it, constant(0, it, line), it, bt, line), bt, bt, line);
    return toBool(r, bt, line);
}

// obj + vbptr offset + vbtable[vbindex] (cl: a virtual base's subobject; entry 0 leads to the vbptr's own)
int IrGen::mpVbaseAddress(int obj, Symbol* cls, int vbindex, int line)
{
    const sema::Type* cp = m_semaTypes.pointer(m_semaTypes.classType(cls));
    const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
    const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
    const sema::Type* ip = m_semaTypes.pointer(it);
    const sema::Type* cpp = m_semaTypes.pointer(ip);
    const int at = cls->vbptrOffset > 0 ? offsetAddress(obj, cp, cp, cls->vbptrOffset, line) : obj;
    LValue pl; pl.kind = LValue::Mem; pl.addr = at; pl.type = ip;
    (void)cpp;
    const int table = load(pl, ip, line);
    IrInst ea = make(IrInst::AddrIndex, ip, line);
    ea.extra = 1;
    ea.args.push_back(IrOperand::temp(table, oldType(ip)));
    ea.args.push_back(IrOperand::temp(convert(vbindex, it, lng, line), oldType(lng)));
    LValue el; el.kind = LValue::Mem; el.addr = emit(ea); el.type = it;
    const int disp = convert(load(el, it, line), it, lng, line);
    IrInst in = make(IrInst::AddrIndex, cp, line);
    in.extra = 1;
    in.args.push_back(IrOperand::temp(at, oldType(cp)));
    in.args.push_back(IrOperand::temp(disp, oldType(lng)));
    return emit(in);
}

// obj.*pm, p->*pm: the member's address for a data member; for a function the adjusted this, with *code the
// function to call
int IrGen::mpApply(cxx::Expr* e, int* code)
{
    const int line = e->loc.line;
    cxx::Expr* objE = e->kids[0];
    cxx::Expr* mpE = e->kids[1];
    const sema::Type* mpT = m_semaTypes.unqualified(m_semaTypes.unref(exprType(mpE)));
    Symbol* mc = mpT->memberClass && mpT->memberClass->kind == sema::Type::Class ? mpT->memberClass->decl : 0;
    const sema::Type* objT = m_semaTypes.unref(exprType(objE));
    int obj;
    if (e->text == "->*") { obj = value(objE); if (objT->kind == sema::Type::Pointer) objT = objT->inner; }
    else { LValue ol = lvalue(objE); obj = ol.kind == LValue::None ? value(objE) : addressOf(ol, line); }
    objT = m_semaTypes.unqualified(objT);
    const int mp = value(mpE);
    if (!mc) return obj;
    const sema::Type* cp = m_semaTypes.pointer(m_semaTypes.classType(mc));
    if (objT->kind == sema::Type::Class && objT->decl && objT->decl != mc) obj = convert(obj, m_semaTypes.pointer(objT), cp, line);   // the base part (never null here)
    const MpLayout l = mpLayout(mpT);
    const int k = mpKind(mpT);
    const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
    const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
    int base = obj;
    if (l.vbindexAt >= 0 && hasVbases(mc)) {
        const int vbi = mpLoad(mp, l.vbindexAt, it, line);
        if (l.unspecified) {
            // cl: the object itself unless the member is in a virtual base
            const int slot = newLocal("tmp.mp.base", cp, false);
            LValue sl; sl.kind = LValue::Local; sl.local = slot; sl.type = cp;
            store(sl, obj, cp, line);
            const int vb = m_out->newBlock("mp.vbase"), jb = m_out->newBlock("mp.base");
            condbr(toBool(vbi, it, line), vb, jb, line);
            m_block = vb;
            store(sl, mpVbaseAddress(obj, mc, vbi, line), cp, line);
            br(jb, line);
            m_block = jb;
            base = load(sl, cp, line);
        } else base = mpVbaseAddress(obj, mc, vbi, line);
    }
    if (!l.function) {
        const sema::Type* mt = m_semaTypes.unref(exprType(e));
        const int off = convert(k == 1 ? mp : mpLoad(mp, 0, it, line), it, lng, line);
        IrInst in = make(IrInst::AddrIndex, m_semaTypes.pointer(mt), line);
        in.extra = 1;
        in.args.push_back(IrOperand::temp(base, oldType(cp)));
        in.args.push_back(IrOperand::temp(off, oldType(lng)));
        return emit(in);
    }
    const sema::Type* pf = m_semaTypes.pointer(mpT->inner);
    if (code) *code = k == 2 ? mp : mpLoad(mp, 0, pf, line);
    if (l.adjAt < 0) return base;
    const int adj = convert(mpLoad(mp, l.adjAt, it, line), it, lng, line);
    IrInst in = make(IrInst::AddrIndex, cp, line);
    in.extra = 1;
    in.args.push_back(IrOperand::temp(base, oldType(cp)));
    in.args.push_back(IrOperand::temp(adj, oldType(lng)));
    return emit(in);
}

// (obj.*pmf)(args), (p->*pmf)(args): an indirect call with the adjusted this; a class result takes the hidden
// pointer after this, a pointer-to-member result of more than 8 bytes before it, as cl passes them
int IrGen::memberPointerCall(cxx::Expr* e, cxx::Expr* callee, int sretAddr)
{
    const int line = e->loc.line;
    int code = -1;
    const int thisPtr = mpApply(callee, &code);
    const sema::Type* mpT = m_semaTypes.unqualified(m_semaTypes.unref(exprType(callee->kids[1])));
    Symbol* mc = mpT->memberClass && mpT->memberClass->kind == sema::Type::Class ? mpT->memberClass->decl : 0;
    const sema::Type* ft = mpT->inner;
    const sema::Type* rt = ft && ft->kind == sema::Type::Function ? ft->inner : m_semaTypes.basic(sema::Type::Void);
    const bool sret = (isClassType(rt) || mpKind(rt) == 3) && !isReference(rt);
    const sema::Type* retReg = sret && mpKind(rt) == 3 ? regReturnType(0, rt) : 0;
    IrInst in = make(IrInst::Call, retReg ? retReg : sret ? m_semaTypes.basic(sema::Type::Void) : rt, line);
    in.sub = "(indirect)";
    in.args.push_back(IrOperand::temp(code, oldType(m_semaTypes.pointer(ft))));
    in.args.push_back(IrOperand::temp(thisPtr, oldType(mc ? m_semaTypes.pointer(m_semaTypes.classType(mc)) : m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void)))));
    int retSlot = -1;
    if (sret) {
        const sema::Type* ru = m_semaTypes.unqualified(m_semaTypes.unref(rt));
        if (sretAddr < 0) { retSlot = newLocal("tmp.ret", ru, true); LValue tl; tl.kind = LValue::Local; tl.local = retSlot; tl.type = ru; sretAddr = addressOf(tl, line); }
        if (!retReg) {
            const IrOperand sp = IrOperand::temp(sretAddr, oldType(m_semaTypes.pointer(ru)));
            if (mpKind(rt) == 3) in.args.insert(in.args.begin() + 1, sp); else in.args.push_back(sp);
        }
    }
    for (size_t a = 1; a < e->kids.size(); ++a) {
        const sema::Type* pt = ft && ft->kind == sema::Type::Function && a - 1 < ft->params.size() ? ft->params[a - 1] : 0;
        if (!pt && isClassType(exprType(e->kids[a])) && !isReference(exprType(e->kids[a]))) pt = m_semaTypes.unqualified(m_semaTypes.unref(exprType(e->kids[a])));
        const int v = argument(e->kids[a], pt, line);
        in.args.push_back(IrOperand::temp(v, oldType(argOperandType(pt, e->kids[a]))));
    }
    if (isReference(rt)) in.type = oldType(m_semaTypes.pointer(rt->inner));
    const int r = emit(in);
    if (retReg) { LValue m; m.kind = LValue::Mem; m.addr = sretAddr; m.type = retReg; store(m, r, retReg, line); }
    if (retSlot >= 0 && needsDtor(rt)) registerCleanup(retSlot, rt, false, 1, line);
    return sret ? sretAddr : r;
}

// the static contents of a pointer to member initialised by x (0: null): &C::m converted to t as cl folds it
bool IrGen::memberPointerData(const sema::Type* t, cxx::Expr* x, std::vector<IrDataItem>& out)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    const MpLayout l = mpLayout(u);
    Symbol* tc = u->memberClass && u->memberClass->kind == sema::Type::Class ? u->memberClass->decl : 0;
    std::vector<long> words(static_cast<size_t>(l.size / 4), 0);
    std::string address;
    while (x && x->kind == cxx::Expr::Paren && x->kids.size() == 1) x = x->kids[0];
    if (!x || (x->semaConst && x->semaValue == 0 && x->semaType && x->semaType->isIntegral())) {
        if (l.vbindexAt >= 0 && (!l.function || l.unspecified)) words[static_cast<size_t>(l.vbindexAt / 4)] = -1;
        else if (!l.function) words[0] = -1;
    } else {
        if (x->kind != cxx::Expr::Unary || x->text != "&" || x->kids.size() != 1) return false;
        Symbol* m = x->kids[0]->semaSym;
        if (!isNonStaticMember(m) || !tc) return false;
        int off = 0, index = 0, rest = 0;
        if (!mpPath(tc, m->ownerClass, off, index, rest)) return false;
        if (l.function) {
            need(m);
            address = m->isVirtual ? vcallThunk(m) : irName(m);
            const long adj = m->thisAdjust + (index ? rest : (l.virt ? off - vbptrOrigin(tc) : off));
            if (l.adjAt < 0 && adj) return false;
            if (l.adjAt >= 0) words[static_cast<size_t>(l.adjAt / 4)] = adj;
        } else words[0] = m->offset + (index ? rest : (l.virt && !l.unspecified ? off - vbptrOrigin(tc) : off));
        if (index) {
            if (l.vbindexAt < 0) return false;
            words[static_cast<size_t>(l.vbindexAt / 4)] = 4 * index;
            if (l.vbptrAt >= 0) words[static_cast<size_t>(l.vbptrAt / 4)] = tc->vbptrOffset;
        }
    }
    size_t w = 0;
    if (l.function) {
        IrDataItem p;
        p.size = 8;
        if (address.empty()) { p.kind = IrDataItem::Zero; }
        else { p.kind = IrDataItem::Address; p.text = address; }
        if (p.kind == IrDataItem::Zero) zeroData(out, 8); else out.push_back(p);
        w = 2;
    }
    for (; w < words.size(); ++w) {
        if (words[w] == 0) { zeroData(out, 4); continue; }
        IrDataItem d; d.kind = IrDataItem::Int; d.size = 4; d.value = words[w];
        out.push_back(d);
    }
    return true;
}

// the bytes of a zero-initialised object that holds pointers to members, as cl writes them for static storage: a
// pointer to member and an array of them (any depth) are null; a class without bases has its own members of pointer
// to member type null (not arrays of them, not those of nested classes); everything else is zero. direct: whether
// t is the object itself or an element (a class in an array is zero). False when every byte is zero
bool IrGen::nullMemberPointerData(const sema::Type* t, std::vector<IrDataItem>& out, bool direct)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (!u || isReference(t)) return false;
    const int size = sizeOf(u);
    if (u->kind == sema::Type::MemberPointer) {
        std::vector<IrDataItem> items;
        if (!memberPointerData(u, 0, items)) return false;
        bool nonZero = false;
        for (size_t i = 0; i < items.size(); ++i) if (items[i].kind != IrDataItem::Zero) nonZero = true;
        if (!nonZero) return false;
        out.insert(out.end(), items.begin(), items.end());
        return true;
    }
    if (u->kind == sema::Type::Array) {
        const sema::Type* e = u->inner;
        while (e && e->kind == sema::Type::Array) e = e->inner;
        if (!e || e->kind != sema::Type::MemberPointer || u->arraySize <= 0) return false;
        for (long i = 0; i < u->arraySize; ++i) if (!nullMemberPointerData(u->inner, out, false)) return false;
        return true;
    }
    if (u->kind != sema::Type::Class || !u->decl || !u->decl->layoutDone || !direct || !u->decl->bases.empty()) return false;
    std::vector<IrDataItem> items;
    int at = 0;
    bool any = false;
    for (size_t f = 0; f < u->decl->fields.size(); ++f) {
        Symbol* fld = u->decl->fields[f];
        if (fld->offset < at || isReference(fld->type)) continue;
        const sema::Type* ft = m_semaTypes.unqualified(fld->type);
        if (ft->kind != sema::Type::MemberPointer || fld->bitWidth >= 0) continue;
        std::vector<IrDataItem> one;
        if (!memberPointerData(ft, 0, one)) continue;
        zeroData(items, fld->offset - at);
        items.insert(items.end(), one.begin(), one.end());
        at = fld->offset + sizeOf(ft);
        any = true;
    }
    if (!any || at > size) return false;
    zeroData(items, size - at);
    out.insert(out.end(), items.begin(), items.end());
    return true;
}

// an element a brace list leaves out, after the object was zeroed: a pointer to member (also the elements of an
// array of them) is set to null, as cl does (a class left out stays zero)
void IrGen::nullMemberPointers(const sema::Type* t, int addr, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (!u) return;
    if (u->kind == sema::Type::MemberPointer) {
        if (mpKind(u) == 2 || (mpLayout(u).function && !mpLayout(u).unspecified)) return;   // null is all zero
        LValue m; m.kind = LValue::Mem; m.addr = addr; m.type = u;
        store(m, mpNull(u, line), u, line);
        return;
    }
    if (u->kind != sema::Type::Array || u->arraySize <= 0) return;
    const sema::Type* e = u->inner;
    while (e && e->kind == sema::Type::Array) e = e->inner;
    if (!e || e->kind != sema::Type::MemberPointer) return;
    const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
    for (long i = 0; i < u->arraySize; ++i) {
        IrInst ai = make(IrInst::AddrIndex, m_semaTypes.pointer(u->inner), line);
        ai.extra = sizeOf(u->inner);
        ai.args.push_back(IrOperand::temp(addr, oldType(m_semaTypes.pointer(u->inner))));
        ai.args.push_back(IrOperand::imm(i, oldType(lng)));
        nullMemberPointers(u->inner, emit(ai), line);
    }
}

// the bases a handler may catch a top object as (C++98 15.3/3): each base subobject reached by public derivation
// only, with its offset in top, unless the class has more than one subobject of that type (ambiguous)
void IrGen::catchableBases(Symbol* top, std::vector<Symbol*>& bases, std::vector<int>& offsets) const
{
    std::vector<Symbol*> todo, seen;
    std::vector<int> offs, seenOffs;
    std::vector<bool> pub, seenPub;
    todo.push_back(top);
    offs.push_back(0);
    pub.push_back(true);
    for (size_t i = 0; i < todo.size() && i < 256; ++i)
        for (size_t b = 0; b < todo[i]->bases.size(); ++b) {
            Symbol* base = todo[i]->bases[b].cls;
            if (!base) continue;
            int off = offs[i] + todo[i]->bases[b].offset;
            if (todo[i]->bases[b].isVirtual) {
                off = 0;
                for (size_t v = 0; v < top->vbases.size() && v < top->vbaseOffsets.size(); ++v) if (top->vbases[v] == base) off = static_cast<int>(top->vbaseOffsets[v]);
            }
            const bool isPublic = pub[i] && todo[i]->bases[b].access == cxx::AccessPublic;
            todo.push_back(base);
            offs.push_back(off);
            pub.push_back(isPublic);
            // two paths to one subobject (a virtual base) meet at the same offset
            size_t k = 0;
            while (k < seen.size() && !(seen[k] == base && seenOffs[k] == off)) ++k;
            if (k < seen.size()) { if (isPublic) seenPub[k] = true; continue; }
            seen.push_back(base);
            seenOffs.push_back(off);
            seenPub.push_back(isPublic);
        }
    for (size_t k = 0; k < seen.size(); ++k) {
        bool unique = true;
        for (size_t j = 0; j < seen.size(); ++j) if (j != k && seen[j] == seen[k]) unique = false;
        if (unique && seenPub[k]) { bases.push_back(seen[k]); offsets.push_back(seenOffs[k]); }
    }
}

// the runtime's type descriptor of a thrown object: its type, then what else a handler may catch it as, separated by
// ';'. A class lists its catchable bases with their offsets ("Base@16": the handler gets the base subobject); a
// pointer to a class lists pointers to them ("const Base*+16": the pointer is adjusted unless null), and a pointer to
// an object type ends with void*. A thrown object is a complete object of its class, so its virtual bases lie at the
// class's offsets; a thrown pointer may point to an object of a derived class, so a virtual base of the pointee is
// found through the object's vbtable ("Base*+v8:4+16": the vbptr at 8, the base's entry at 4 in its table, 16
// further for a non-virtual base of that virtual base). A handler of pointer type also takes the thrown pointer with
// more cv-qualifiers on the pointee, which the runtime checks
std::string IrGen::throwDescriptorFor(const sema::Type* t) const
{
    t = m_semaTypes.unref(t);
    if (!t) return "unknown";
    t = m_semaTypes.unqualified(t);
    std::string d = t->kind == sema::Type::Class && t->decl ? t->decl->qualifiedName() : t->text();
    const sema::Type* pointee = t->kind == sema::Type::Pointer ? t->inner : 0;
    Symbol* top = t->kind == sema::Type::Class ? t->decl : pointee && pointee->kind == sema::Type::Class ? pointee->decl : 0;
    const std::string cv = pointee ? std::string(pointee->isConst ? "const " : "") + (pointee->isVolatile ? "volatile " : "") : std::string();
    if (top) {
        std::vector<Symbol*> bases;
        std::vector<int> offsets;
        catchableBases(top, bases, offsets);
        for (size_t k = 0; k < bases.size(); ++k) {
            d += ";" + cv + bases[k]->qualifiedName() + (pointee ? "*" : "");
            int off = 0, index = 0, rest = 0;
            if (pointee && !baseOffset(top, bases[k], off) && virtualBasePath(top, bases[k], index, rest)) {
                d += "+v" + numText(static_cast<long>(top->vbptrOffset)) + ":" + numText(4L * index);
                if (rest) d += "+" + numText(static_cast<long>(rest));
            } else if (offsets[k]) d += (pointee ? "+" : "@") + numText(static_cast<long>(offsets[k]));
        }
    }
    if (pointee && pointee->kind != sema::Type::Function && pointee->kind != sema::Type::Void) d += ";" + cv + "void*";
    return d;
}

const sema::Type* IrGen::exprType(cxx::Expr* e) const
{
    return e && e->semaType ? e->semaType : m_semaTypes.basic(sema::Type::Long);
}

// ---- emission helpers --------------------------------------------------------------------------

IrInst IrGen::make(IrInst::Op op, const sema::Type* type, int line)
{
    IrInst in;
    in.op = op;
    in.type = type ? oldType(type) : 0;
    in.line = line;
    return in;
}

bool IrGen::terminated() const
{
    const IrBlock& b = m_out->blocks[static_cast<size_t>(m_block)];
    return !b.insts.empty() && b.insts.back().isTerminator();
}

int IrGen::emit(IrInst inst)
{
    if (terminated()) m_block = m_out->newBlock("dead");
    bool producesValue = inst.op == IrInst::Const || inst.op == IrInst::FConst || inst.op == IrInst::StrConst || inst.op == IrInst::Copy ||
                         inst.op == IrInst::LoadLocal || inst.op == IrInst::AddrLocal || inst.op == IrInst::LoadGlobal || inst.op == IrInst::AddrGlobal ||
                         inst.op == IrInst::LoadMem || inst.op == IrInst::AddrField || inst.op == IrInst::AddrIndex || inst.op == IrInst::Bin ||
                         inst.op == IrInst::Un || inst.op == IrInst::Cmp || inst.op == IrInst::Conv || inst.op == IrInst::CatchParam ||
                         ((inst.op == IrInst::Call || inst.op == IrInst::VCall) && inst.type && !inst.type->isVoid());
    if (producesValue && inst.dst < 0) inst.dst = m_out->newTemp(inst.type);
    m_out->blocks[static_cast<size_t>(m_block)].insts.push_back(inst);
    return inst.dst;
}

void IrGen::br(int target, int line)
{
    if (terminated()) return;
    IrInst in = make(IrInst::Br, 0, line);
    in.args.push_back(IrOperand::block(target));
    emit(in);
}

void IrGen::condbr(int cond, int t, int f, int line)
{
    IrInst in = make(IrInst::CondBr, 0, line);
    in.args.push_back(IrOperand::temp(cond, m_types.boolType()));
    in.args.push_back(IrOperand::block(t));
    in.args.push_back(IrOperand::block(f));
    emit(in);
}

int IrGen::constant(long v, const sema::Type* t, int line)
{
    if (t && t->isFloating()) {
        IrInst fin = make(IrInst::FConst, t, line);
        fin.args.push_back(IrOperand::fimm(static_cast<double>(v), fin.type));
        return emit(fin);
    }
    IrInst in = make(IrInst::Const, t, line);
    in.args.push_back(IrOperand::imm(v, in.type));
    return emit(in);
}

int IrGen::thisTemp(int line)
{
    IrInst in = make(IrInst::LoadLocal, 0, line);
    in.type = m_out->locals[static_cast<size_t>(m_thisSlot)].type;
    in.args.push_back(IrOperand::local(m_thisSlot, in.type));
    return emit(in);
}

int IrGen::newLocal(const std::string& name, const sema::Type* t, bool addressTaken)
{
    IrLocal l;
    l.name = name;
    l.type = oldType(t);
    l.size = isReference(t) ? 8 : sizeOf(t);
    l.align = isReference(t) ? 8 : alignOf(t);
    if (l.size <= 0) l.size = 8;
    if (l.align <= 0) l.align = 8;
    l.addressTaken = addressTaken;
    m_out->locals.push_back(l);
    return static_cast<int>(m_out->locals.size() - 1);
}

int IrGen::addressOf(const LValue& lv, int line)
{
    const sema::Type* u = lv.type;
    if (lv.kind == LValue::Local) {
        m_out->locals[static_cast<size_t>(lv.local)].addressTaken = true;
        IrInst in = make(IrInst::AddrLocal, m_semaTypes.pointer(u), line);
        in.args.push_back(IrOperand::local(lv.local, oldType(u)));
        return emit(in);
    }
    if (lv.kind == LValue::Global) {
        IrInst in = make(IrInst::AddrGlobal, m_semaTypes.pointer(u), line);
        in.args.push_back(IrOperand::global(lv.global, oldType(u)));
        return emit(in);
    }
    return lv.addr;
}

int IrGen::load(const LValue& lv, const sema::Type* t, int line)
{
    const sema::Type* u = m_semaTypes.unref(t);
    if (byAddress(u)) return addressOf(lv, line);
    if (lv.bitWidth > 0 && lv.kind == LValue::Mem) return loadBits(lv, u, line);
    if (lv.kind == LValue::Local) { IrInst in = make(IrInst::LoadLocal, u, line); in.args.push_back(IrOperand::local(lv.local, oldType(u))); return emit(in); }
    if (lv.kind == LValue::Global) { IrInst in = make(IrInst::LoadGlobal, u, line); in.args.push_back(IrOperand::global(lv.global, oldType(u))); return emit(in); }
    if (lv.kind == LValue::Mem) { IrInst in = make(IrInst::LoadMem, u, line); in.args.push_back(IrOperand::temp(lv.addr, oldType(m_semaTypes.pointer(u)))); return emit(in); }
    return constant(0, u, line);
}

void IrGen::store(const LValue& lv, int temp, const sema::Type* t, int line)
{
    const sema::Type* u = m_semaTypes.unref(t);
    if (byAddress(u)) {
        int dst = addressOf(lv, line);
        IrInst c = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
        c.sub = "__copy";
        c.args.push_back(IrOperand::temp(dst, oldType(m_semaTypes.pointer(u))));
        c.args.push_back(IrOperand::temp(temp, oldType(m_semaTypes.pointer(u))));
        c.args.push_back(IrOperand::imm(sizeOf(u), m_types.intType()));
        emit(c);
        return;
    }
    if (lv.bitWidth > 0 && lv.kind == LValue::Mem) { storeBits(lv, temp, u, line); return; }
    if (lv.kind == LValue::Local) { IrInst in = make(IrInst::StoreLocal, u, line); in.args.push_back(IrOperand::local(lv.local, oldType(u))); in.args.push_back(IrOperand::temp(temp, oldType(u))); emit(in); return; }
    if (lv.kind == LValue::Global) { IrInst in = make(IrInst::StoreGlobal, u, line); in.args.push_back(IrOperand::global(lv.global, oldType(u))); in.args.push_back(IrOperand::temp(temp, oldType(u))); emit(in); return; }
    if (lv.kind == LValue::Mem) { IrInst in = make(IrInst::StoreMem, u, line); in.args.push_back(IrOperand::temp(lv.addr, oldType(m_semaTypes.pointer(u)))); in.args.push_back(IrOperand::temp(temp, oldType(u))); emit(in); return; }
    m_notes.push_back("L" + numText(line) + ": store to a non-lvalue was dropped");
}

// ---- bit-fields: the unit (the declared type's size, at the field's offset) is read and written whole ----

void IrGen::bitField(LValue& lv, const Symbol* field) const
{
    if (!field || field->bitWidth <= 0 || lv.kind != LValue::Mem) return;
    lv.bitWidth = field->bitWidth;
    lv.bitOffset = field->bitOffset;
}

// a constant of the 32- or 64-bit unsigned type t; IR immediates are 32-bit and sign-extended, so a 64-bit
// value outside that range is put together from pieces
int IrGen::bitConstant(unsigned long long v, const sema::Type* t, int line)
{
    if (sizeOf(t) < 8) return constant(static_cast<long>(static_cast<int>(static_cast<unsigned int>(v))), t, line);
    const long long sv = static_cast<long long>(v);
    if (sv >= -2147483647LL - 1 && sv <= 2147483647LL) return constant(static_cast<long>(sv), t, line);
    const unsigned long lo = static_cast<unsigned long>(v & 0xFFFFFFFFULL);
    // one constant per statement: the order of a call's arguments is unspecified (cl and cppparse differ)
    const int top = constant(static_cast<long>(static_cast<int>(static_cast<unsigned int>(v >> 32))), t, line);
    const int by32 = constant(32, t, line);
    const int hi = binary("<<", top, t, by32, t, t, line);
    const int midBits = constant(static_cast<long>(lo >> 16), t, line);
    const int by16 = constant(16, t, line);
    const int mid = binary("<<", midBits, t, by16, t, t, line);
    const int r = binary("|", hi, t, mid, t, t, line);
    const int low = constant(static_cast<long>(lo & 0xFFFFUL), t, line);
    return binary("|", r, t, low, t, t, line);
}

namespace {

// the unsigned integer type of a bit-field's unit, and the 32- or 64-bit types its bits are worked in
sema::Type::Kind unitKind(int size) { return size == 1 ? sema::Type::UChar : size == 2 ? sema::Type::UShort : size == 8 ? sema::Type::ULongLong : sema::Type::UInt; }

} // namespace

int IrGen::loadBits(const LValue& lv, const sema::Type* t, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    const sema::Type* unit = m_semaTypes.basic(unitKind(sizeOf(u)));
    const int bits = sizeOf(u) == 8 ? 64 : 32;
    const sema::Type* wu = m_semaTypes.basic(bits == 64 ? sema::Type::ULongLong : sema::Type::UInt);
    const sema::Type* ws = m_semaTypes.basic(bits == 64 ? sema::Type::LongLong : sema::Type::Int);
    IrInst in = make(IrInst::LoadMem, unit, line);
    in.args.push_back(IrOperand::temp(lv.addr, oldType(m_semaTypes.pointer(unit))));
    int x = convert(emit(in), unit, wu, line);
    if (!u->isUnsigned()) {
        // signed: the field's top bit to the top, then an arithmetic shift down
        x = convert(x, wu, ws, line);
        const int up = bits - lv.bitOffset - lv.bitWidth;
        if (up > 0) x = binary("<<", x, ws, constant(up, ws, line), ws, ws, line);
        if (lv.bitWidth < bits) x = binary(">>", x, ws, constant(bits - lv.bitWidth, ws, line), ws, ws, line);
        return convert(x, ws, u, line);
    }
    if (lv.bitOffset > 0) x = binary(">>", x, wu, constant(lv.bitOffset, wu, line), wu, wu, line);
    if (lv.bitWidth < bits) x = binary("&", x, wu, bitConstant((1ULL << lv.bitWidth) - 1, wu, line), wu, wu, line);
    return convert(x, wu, u, line);
}

void IrGen::storeBits(const LValue& lv, int temp, const sema::Type* t, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    const sema::Type* unit = m_semaTypes.basic(unitKind(sizeOf(u)));
    const int bits = sizeOf(u) == 8 ? 64 : 32;
    const sema::Type* wu = m_semaTypes.basic(bits == 64 ? sema::Type::ULongLong : sema::Type::UInt);
    const unsigned long long all = bits == 64 ? ~0ULL : 0xFFFFFFFFULL;
    const unsigned long long field = (lv.bitWidth >= 64 ? all : ((1ULL << lv.bitWidth) - 1) << lv.bitOffset) & all;
    const sema::Type* pu = m_semaTypes.pointer(unit);
    IrInst ld = make(IrInst::LoadMem, unit, line);
    ld.args.push_back(IrOperand::temp(lv.addr, oldType(pu)));
    int old = binary("&", convert(emit(ld), unit, wu, line), wu, bitConstant(~field & all, wu, line), wu, wu, line);
    int v = convert(temp, u, wu, line);
    if (lv.bitOffset > 0) v = binary("<<", v, wu, constant(lv.bitOffset, wu, line), wu, wu, line);
    v = binary("&", v, wu, bitConstant(field, wu, line), wu, wu, line);
    int merged = convert(binary("|", old, wu, v, wu, wu, line), wu, unit, line);
    IrInst st = make(IrInst::StoreMem, unit, line);
    st.args.push_back(IrOperand::temp(lv.addr, oldType(pu)));
    st.args.push_back(IrOperand::temp(merged, oldType(unit)));
    emit(st);
}

int IrGen::toBool(int temp, const sema::Type* t, int line)
{
    t = m_semaTypes.decay(t);
    if (!t || t->kind == sema::Type::Bool) return temp;
    if (t->kind == sema::Type::MemberPointer) return mpToBool(temp, t, line);
    IrInst in = make(IrInst::Cmp, m_semaTypes.basic(sema::Type::Bool), line);
    if (t->isFloating()) { in.sub = "fne"; in.args.push_back(IrOperand::temp(temp, oldType(t))); in.args.push_back(IrOperand::fimm(0.0, oldType(t))); }
    else { in.sub = "ne"; in.args.push_back(IrOperand::temp(temp, oldType(t))); in.args.push_back(IrOperand::imm(0, oldType(t))); }
    return emit(in);
}

int IrGen::offsetAddress(int temp, const sema::Type* fromPtr, const sema::Type* toPtr, long delta, int line)
{
    IrInst in = make(IrInst::AddrIndex, toPtr, line);
    in.extra = 1;
    in.args.push_back(IrOperand::temp(temp, oldType(fromPtr)));
    in.args.push_back(IrOperand::imm(delta, m_types.builtin(Type::LongLong)));
    return emit(in);
}

int IrGen::convert(int temp, const sema::Type* from, const sema::Type* to, int line)
{
    from = m_semaTypes.decay(m_semaTypes.unqualified(m_semaTypes.unref(from)));
    to = m_semaTypes.unqualified(m_semaTypes.unref(to));
    if (!from || !to) return temp;
    from = m_semaTypes.unqualified(from);
    if (from == to) return temp;
    if (to->kind == sema::Type::MemberPointer) {
        if (from->kind == sema::Type::MemberPointer) return mpConvert(temp, from, to, line);
        if (from->isIntegral() || from->kind == sema::Type::NullPtr) return mpNull(to, line);   // the null pointer constant
        return temp;
    }
    if (from->kind == sema::Type::MemberPointer) return to->kind == sema::Type::Bool ? mpToBool(temp, from, line) : temp;
    // pointer and reference conversions to a base class adjust the address
    if ((from->kind == sema::Type::Pointer && to->kind == sema::Type::Pointer) || (from->kind == sema::Type::Class && to->kind == sema::Type::Class)) {
        const sema::Type* fc = from->kind == sema::Type::Pointer ? m_semaTypes.unqualified(from->inner) : from;
        const sema::Type* tc = to->kind == sema::Type::Pointer ? m_semaTypes.unqualified(to->inner) : to;
        if (fc->kind == sema::Type::Class && tc->kind == sema::Type::Class && fc->decl != tc->decl) {
            int off = 0;
            long delta = 0;
            int index = 0, rest = 0;
            const bool viaVbase = !baseOffset(fc->decl, tc->decl, off) && virtualBasePath(fc->decl, tc->decl, index, rest);
            if (baseOffset(fc->decl, tc->decl, off) && off > 0) delta = off;                 // derived to base
            else if (baseOffset(tc->decl, fc->decl, off) && off > 0) delta = -off;           // base to derived (static_cast)
            int dummyIndex = 0, dummyRest = 0;
            if (!viaVbase && !baseOffset(tc->decl, fc->decl, off) && virtualBasePath(tc->decl, fc->decl, dummyIndex, dummyRest)) {
                // ill-formed (cl C2635): a virtual base cannot be converted down to the derived class
                unsupported(line, "cannot convert from '" + fc->decl->qualifiedName() + "' to '" + tc->decl->qualifiedName() + "': '" + fc->decl->qualifiedName() + "' is a virtual base");
                return temp;
            }
            if (viaVbase) {
                // through the vbtable; a null pointer stays null
                const sema::Type* fp = from->kind == sema::Type::Pointer ? from : m_semaTypes.pointer(from);
                const sema::Type* tp = to->kind == sema::Type::Pointer ? to : m_semaTypes.pointer(to);
                if (from->kind != sema::Type::Pointer) return toBase(temp, fc->decl, tc->decl, line);
                int slot = newLocal("tmp.cast", tp, false);
                LValue lv; lv.kind = LValue::Local; lv.local = slot; lv.type = tp;
                store(lv, constant(0, tp, line), tp, line);
                int nb = m_out->newBlock("cast.nonnull"), jb = m_out->newBlock("cast.end");
                condbr(toBool(temp, fp, line), nb, jb, line);
                m_block = nb;
                store(lv, toBase(temp, fc->decl, tc->decl, line), tp, line);
                br(jb, line);
                m_block = jb;
                return load(lv, tp, line);
            }
            if (delta != 0) {
                const sema::Type* fp = from->kind == sema::Type::Pointer ? from : m_semaTypes.pointer(from);
                const sema::Type* tp = to->kind == sema::Type::Pointer ? to : m_semaTypes.pointer(to);
                if (from->kind != sema::Type::Pointer) return offsetAddress(temp, fp, tp, delta, line);   // an object: never null
                // a null pointer stays null
                int slot = newLocal("tmp.cast", tp, false);
                LValue lv; lv.kind = LValue::Local; lv.local = slot; lv.type = tp;
                store(lv, constant(0, tp, line), tp, line);
                int nb = m_out->newBlock("cast.nonnull"), jb = m_out->newBlock("cast.end");
                condbr(toBool(temp, fp, line), nb, jb, line);
                m_block = nb;
                store(lv, offsetAddress(temp, fp, tp, delta, line), tp, line);
                br(jb, line);
                m_block = jb;
                return load(lv, tp, line);
            }
        }
        return temp;
    }
    if (to->kind == sema::Type::Class || from->kind == sema::Type::Class || to->kind == sema::Type::Array || to->kind == sema::Type::Error || from->kind == sema::Type::Error) return temp;
    if (to->kind == sema::Type::Bool) return toBool(temp, from, line);
    if (from->isFloating() && to->isIntegral()) {
        // a floating constant converted to an integer is folded as cl folds it, modulo 2^N ((int)3e9 is
        // B2D05E00h, where the instruction gives 80000000h)
        const IrBlock& b = m_out->blocks[static_cast<size_t>(m_block)];
        unsigned long long bits = 0;
        double raw = 0;
        if (!b.insts.empty() && b.insts.back().op == IrInst::FConst && b.insts.back().dst == temp && b.insts.back().args.size() == 1
            && foldFloatToInteger(b.insts.back().args[0].fval, sizeOf(to) * 8, to->isUnsigned(), bits, raw)) {
            const int r = sizeOf(to) == 8 ? bitConstant(bits, to, line) : constant(static_cast<long>(static_cast<int>(static_cast<unsigned int>(bits & 0xFFFFFFFFULL))), to, line);
            m_rawTemp = r;
            m_rawFunction = m_out;
            m_rawBlock = m_block;
            m_rawEnd = m_out->blocks[static_cast<size_t>(m_block)].insts.size();
            m_raw = raw;
            return r;
        }
    }
    if (from->isIntegral() && to->isFloating() && temp == m_rawTemp && m_out == m_rawFunction && m_block == m_rawBlock && m_out->blocks[static_cast<size_t>(m_block)].insts.size() == m_rawEnd) {
        IrInst in = make(IrInst::FConst, to, line);
        in.args.push_back(IrOperand::fimm(to->kind == sema::Type::Float ? static_cast<double>(static_cast<float>(m_raw)) : m_raw, oldType(to)));
        return emit(in);
    }
    std::string sub;
    if (from->isIntegral() && to->isIntegral()) {
        int fs = sizeOf(from), ts = sizeOf(to);
        if (fs == ts && (fs >= 8 || from->isUnsigned() == to->isUnsigned())) return temp;
        // same size, other signedness: re-extended by the target's signedness (trunc to the same width)
        sub = ts <= fs ? "trunc" : (from->isUnsigned() ? "zext" : "sext");
    } else if (from->isIntegral() && to->isFloating()) sub = from->isUnsigned() && from->kind != sema::Type::Bool ? "uitofp" : "sitofp";
    else if (from->isFloating() && to->isIntegral()) sub = "fptosi";
    else if (from->isFloating() && to->isFloating()) {
        // long double is double (both are lowered to the 8-byte double, as MSVC): only float <-> the others converts
        const bool fromSingle = from->kind == sema::Type::Float, toSingle = to->kind == sema::Type::Float;
        if (fromSingle == toSingle) return temp;
        sub = toSingle ? "fptrunc" : "fpext";
    }
    else if (from->isPointer() && to->isIntegral()) sub = "ptrtoint";
    else if (from->isIntegral() && to->isPointer()) sub = "inttoptr";
    else return temp;
    IrInst in = make(IrInst::Conv, to, line);
    in.sub = sub;
    in.args.push_back(IrOperand::temp(temp, oldType(from)));
    return emit(in);
}

int IrGen::binary(const std::string& op, int a, const sema::Type* ta, int b, const sema::Type* tb, const sema::Type* result, int line)
{
    ta = m_semaTypes.unqualified(m_semaTypes.decay(ta)); tb = m_semaTypes.unqualified(m_semaTypes.decay(tb)); result = m_semaTypes.unqualified(m_semaTypes.decay(result));
    bool cmp = op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=";
    if (cmp && (ta->kind == sema::Type::MemberPointer || tb->kind == sema::Type::MemberPointer)) return mpCompare(a, ta, b, tb, op == "!=", line);
    const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
    if (!cmp && ta->kind == sema::Type::Pointer && tb->isIntegral() && (op == "+" || op == "-")) {
        int idx = convert(b, tb, lng, line);
        if (op == "-") { IrInst n = make(IrInst::Un, lng, line); n.sub = "neg"; n.args.push_back(IrOperand::temp(idx, oldType(lng))); idx = emit(n); }
        IrInst in = make(IrInst::AddrIndex, ta, line);
        in.extra = sizeOf(ta->inner) > 0 ? sizeOf(ta->inner) : 1;
        in.args.push_back(IrOperand::temp(a, oldType(ta)));
        in.args.push_back(IrOperand::temp(idx, oldType(lng)));
        return emit(in);
    }
    if (!cmp && op == "+" && ta->isIntegral() && tb->kind == sema::Type::Pointer) return binary(op, b, tb, a, ta, result, line);
    if (!cmp && op == "-" && ta->kind == sema::Type::Pointer && tb->kind == sema::Type::Pointer) {
        IrInst d = make(IrInst::Bin, lng, line); d.sub = "sub"; d.args.push_back(IrOperand::temp(a, oldType(lng))); d.args.push_back(IrOperand::temp(b, oldType(lng)));
        int diff = emit(d);
        int es = sizeOf(ta->inner) > 0 ? sizeOf(ta->inner) : 1;
        IrInst q = make(IrInst::Bin, lng, line); q.sub = "sdiv"; q.args.push_back(IrOperand::temp(diff, oldType(lng))); q.args.push_back(IrOperand::imm(es, oldType(lng)));
        return emit(q);
    }
    const sema::Type* common = result;
    if (cmp) {
        if (ta->isArithmetic() && tb->isArithmetic()) common = m_semaTypes.arithmeticResult(ta, tb);
        else if (ta->isPointer() || ta->kind == sema::Type::NullPtr) common = ta->kind == sema::Type::NullPtr ? tb : ta;
        else common = tb;
        if (common->kind == sema::Type::NullPtr) common = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
    } else if (!common->isArithmetic()) common = m_semaTypes.arithmeticResult(ta, tb);
    a = convert(a, ta, common, line);
    b = convert(b, tb, common, line);
    bool fl = common->isFloating();
    bool uns = common->isUnsigned() || common->isPointer();
    std::string sub;
    if (cmp) {
        if (op == "==") sub = fl ? "feq" : "eq";
        else if (op == "!=") sub = fl ? "fne" : "ne";
        else if (op == "<") sub = fl ? "flt" : (uns ? "ult" : "slt");
        else if (op == "<=") sub = fl ? "fle" : (uns ? "ule" : "sle");
        else if (op == ">") sub = fl ? "fgt" : (uns ? "ugt" : "sgt");
        else sub = fl ? "fge" : (uns ? "uge" : "sge");
        IrInst in = make(IrInst::Cmp, m_semaTypes.basic(sema::Type::Bool), line);
        in.sub = sub;
        in.args.push_back(IrOperand::temp(a, oldType(common)));
        in.args.push_back(IrOperand::temp(b, oldType(common)));
        return emit(in);
    }
    if (op == "+") sub = fl ? "fadd" : "add";
    else if (op == "-") sub = fl ? "fsub" : "sub";
    else if (op == "*") sub = fl ? "fmul" : "mul";
    else if (op == "/") sub = fl ? "fdiv" : (uns ? "udiv" : "sdiv");
    else if (op == "%") sub = uns ? "urem" : "srem";
    else if (op == "<<") sub = "shl";
    else if (op == ">>") sub = uns ? "lshr" : "ashr";
    else if (op == "&") sub = "and";
    else if (op == "|") sub = "or";
    else if (op == "^") sub = "xor";
    else sub = "add";
    IrInst in = make(IrInst::Bin, common, line);
    in.sub = sub;
    in.args.push_back(IrOperand::temp(a, oldType(common)));
    in.args.push_back(IrOperand::temp(b, oldType(common)));
    int r = emit(in);
    return convert(r, common, result, line);
}

void IrGen::leaveTryRegions(size_t depth, int line)
{
    for (size_t k = m_tryStack.size(); k > depth; --k) {
        if (m_tryStack[k - 1] < 0) { emit(make(IrInst::CatchEnd, 0, line)); continue; }
        IrInst te = make(IrInst::TryEnd, 0, line);
        te.extra = m_tryStack[k - 1];
        emit(te);
    }
}

// ---- lvalues and values ----------------------------------------------------------------------------

IrGen::LValue IrGen::lvalue(cxx::Expr* e)
{
    LValue lv;
    lv.type = m_semaTypes.unref(exprType(e));
    const int line = e->loc.line;
    if (e->kind == cxx::Expr::Binary && (e->text == ".*" || e->text == "->*") && !e->semaSym && e->kids.size() == 2) {
        // a data member through a pointer to member
        lv.kind = LValue::Mem; lv.addr = mpApply(e, 0);
        return lv;
    }
    switch (e->kind) {
    case cxx::Expr::Name: {
        Symbol* s = e->semaSym;
        if (!s || s->kind != Symbol::Variable) break;
        std::map<Symbol*, int>::iterator it = m_slots.find(s);
        if (it != m_slots.end()) {
            int idx = it->second;
            const sema::Type* declared = s->type;
            if (isReference(declared)) {
                IrInst in = make(IrInst::LoadLocal, m_semaTypes.pointer(declared->inner), line);
                in.args.push_back(IrOperand::local(idx, oldType(declared)));
                lv.kind = LValue::Mem; lv.addr = emit(in); lv.type = m_semaTypes.unref(declared);
                return lv;
            }
            lv.kind = LValue::Local; lv.local = idx; lv.type = declared;
            return lv;
        }
        std::map<Symbol*, std::string>::iterator st = m_staticNames.find(s);
        if (st != m_staticNames.end()) {
            lv.kind = LValue::Global; lv.global = st->second; lv.type = m_semaTypes.unref(s->type);
            if (isReference(s->type)) { IrInst ld = make(IrInst::LoadGlobal, m_semaTypes.pointer(s->type->inner), line); ld.args.push_back(IrOperand::global(lv.global, oldType(s->type))); lv.kind = LValue::Mem; lv.addr = emit(ld); }
            return lv;
        }
        if (s->isMember && !s->isStatic) {
            if (!m_out->hasThis) break;
            int self = thisTemp(line);
            int off = s->offset;
            if (m_fn && m_fn->ownerClass && s->ownerClass && m_fn->ownerClass != s->ownerClass) { int bo = 0; if (baseOffset(m_fn->ownerClass, s->ownerClass, bo)) off += bo; else self = toBase(self, m_fn->ownerClass, s->ownerClass, line); }
            IrInst in = make(IrInst::AddrField, m_semaTypes.pointer(lv.type), line);
            in.sub = s->name;
            in.extra = off;
            in.args.push_back(IrOperand::temp(self, m_out->locals[static_cast<size_t>(m_thisSlot)].type));
            if (isReference(s->type)) { IrInst ld = make(IrInst::LoadMem, m_semaTypes.pointer(s->type->inner), line); ld.args.push_back(IrOperand::temp(emit(in), oldType(m_semaTypes.pointer(s->type)))); lv.kind = LValue::Mem; lv.addr = emit(ld); lv.type = s->type->inner; return lv; }
            lv.kind = LValue::Mem; lv.addr = emit(in);
            bitField(lv, s);
            return lv;
        }
        lv.kind = LValue::Global; lv.global = s->qualifiedName();
        if (isReference(s->type)) { IrInst ld = make(IrInst::LoadGlobal, m_semaTypes.pointer(s->type->inner), line); ld.args.push_back(IrOperand::global(lv.global, oldType(s->type))); lv.kind = LValue::Mem; lv.addr = emit(ld); lv.type = s->type->inner; }
        return lv;
    }
    case cxx::Expr::Member: {
        Symbol* s = e->semaSym;
        if (!s || s->kind != Symbol::Variable) break;
        if (s->isStatic) { lv.kind = LValue::Global; lv.global = s->qualifiedName(); return lv; }
        cxx::Expr* obj = e->kids[0];
        const sema::Type* objT = m_semaTypes.unref(exprType(obj));
        int base;
        if (e->arrowMember) {
            base = value(obj);
            if (objT->kind == sema::Type::Pointer) objT = objT->inner;
        } else {
            LValue ol = lvalue(obj);
            base = ol.kind == LValue::None ? value(obj) : load(ol, objT, line);
        }
        objT = m_semaTypes.unqualified(objT);
        int off = s->offset;
        // "d.C::a": through the named base first, the path to the member's class from there
        Symbol* naming = objT->kind == sema::Type::Class ? namingBase(e, objT->decl) : 0;
        if (naming && naming != s->ownerClass && s->ownerClass) {
            int b1 = 0, b2 = 0;
            if (baseOffset(objT->decl, naming, b1) && baseOffset(naming, s->ownerClass, b2)) off += b1 + b2;
            else { base = toBase(base, objT->decl, naming, line); base = toBase(base, naming, s->ownerClass, line); }
        } else
        if (objT->kind == sema::Type::Class && objT->decl && s->ownerClass && objT->decl != s->ownerClass) { int bo = 0; if (baseOffset(objT->decl, s->ownerClass, bo)) off += bo; else base = toBase(base, objT->decl, s->ownerClass, line); }
        IrInst in = make(IrInst::AddrField, m_semaTypes.pointer(lv.type), line);
        in.sub = s->name;
        in.extra = off;
        in.args.push_back(IrOperand::temp(base, oldType(m_semaTypes.pointer(objT))));
        lv.kind = LValue::Mem; lv.addr = emit(in);
        if (isReference(s->type)) { IrInst ld = make(IrInst::LoadMem, m_semaTypes.pointer(s->type->inner), line); ld.args.push_back(IrOperand::temp(lv.addr, oldType(m_semaTypes.pointer(s->type)))); lv.addr = emit(ld); lv.type = s->type->inner; }
        bitField(lv, s);
        return lv;
    }
    case cxx::Expr::Index: {
        if (e->semaSym && e->semaSym->kind == Symbol::Function) { lv.kind = LValue::Mem; lv.addr = operatorCall(e); return lv; }      // operator[] returning a reference
        cxx::Expr* base = e->kids[0];
        cxx::Expr* index = e->kids[1];
        const sema::Type* bt = m_semaTypes.unref(exprType(base));
        if (bt->kind != sema::Type::Pointer && bt->kind != sema::Type::Array) { std::swap(base, index); bt = m_semaTypes.unref(exprType(base)); }
        int basePtr;
        if (bt->kind == sema::Type::Array) { LValue bl = lvalue(base); basePtr = bl.kind == LValue::None ? value(base) : load(bl, bt, line); }
        else basePtr = value(base);
        const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
        int idx = convert(value(index), exprType(index), lng, line);
        IrInst in = make(IrInst::AddrIndex, m_semaTypes.pointer(lv.type), line);
        in.extra = sizeOf(lv.type) > 0 ? sizeOf(lv.type) : 1;
        in.args.push_back(IrOperand::temp(basePtr, oldType(m_semaTypes.pointer(lv.type))));
        in.args.push_back(IrOperand::temp(idx, oldType(lng)));
        lv.kind = LValue::Mem; lv.addr = emit(in);
        return lv;
    }
    case cxx::Expr::Unary:
        if (e->semaSym && e->semaSym->kind == Symbol::Function) { if (e->semaLvalue || isClassType(exprType(e))) { lv.kind = LValue::Mem; lv.addr = operatorCall(e); return lv; } break; }
        if (e->text == "*" && !e->semaSym) { lv.kind = LValue::Mem; lv.addr = value(e->kids[0]); return lv; }
        if ((e->text == "++" || e->text == "--") && !e->semaSym) { value(e); return lvalue(e->kids[0]); }
        break;
    case cxx::Expr::Paren: return lvalue(e->kids[0]);
    case cxx::Expr::Typeid: lv.kind = LValue::Mem; lv.addr = typeidAddress(e, line); return lv;
    case cxx::Expr::Cast: case cxx::Expr::NamedCast:
        if (e->kind == cxx::Expr::NamedCast && e->text == "dynamic_cast" && isReference(exprType(e))) { int r = -1; if (dynamicCast(e, line, r)) { lv.kind = LValue::Mem; lv.addr = r; return lv; } }
        if (!e->kids.empty() && e->semaLvalue) { LValue inner = lvalue(e->kids[0]); if (inner.kind != LValue::None) { lv.kind = LValue::Mem; lv.addr = convert(addressOf(inner, line), m_semaTypes.pointer(inner.type), m_semaTypes.pointer(lv.type), line); } return lv; }
        break;
    case cxx::Expr::Call: case cxx::Expr::FunctionalCast: case cxx::Expr::Binary: case cxx::Expr::PostIncDec:
        if (e->semaLvalue || isClassType(exprType(e))) {
            // the raw call result: an address for a reference, the temporary's address for a class
            lv.kind = LValue::Mem;
            if (e->kind == cxx::Expr::Call) lv.addr = call(e);
            else if (e->kind != cxx::Expr::FunctionalCast && e->semaSym && e->semaSym->kind == Symbol::Function) lv.addr = operatorCall(e);
            else lv.addr = value(e);
            return lv;
        }
        break;
    case cxx::Expr::Assign:
        if (e->semaSym && e->semaSym->kind == Symbol::Function && !(e->semaSym->isImplicit && e->semaSym->name == "operator=")) { lv.kind = LValue::Mem; lv.addr = operatorCall(e); return lv; }
        if (e->semaSym) { lv.kind = LValue::Mem; lv.addr = value(e); return lv; }
        value(e);
        return lvalue(e->kids[0]);
    case cxx::Expr::Comma:
        if (e->semaSym && e->semaSym->kind == Symbol::Function) {   // a user-defined operator,: its result, as a call's
            if (e->semaLvalue || isClassType(exprType(e))) { lv.kind = LValue::Mem; lv.addr = operatorCall(e); return lv; }
            break;
        }
        value(e->kids[0]);
        return lvalue(e->kids[1]);
    case cxx::Expr::Cond:
        if (e->semaLvalue || isClassType(exprType(e))) { lv.kind = LValue::Mem; lv.addr = conditional(e, true); return lv; }
        break;
    case cxx::Expr::StringLit:
        lv.kind = LValue::Mem; lv.addr = value(e); return lv;
    default:
        break;
    }
    return lv;
}

// c ? a : b through a temporary: the chosen operand's address when byAddr (class objects, lvalue
// results), else its converted value
int IrGen::conditional(cxx::Expr* e, bool byAddr)
{
    const int line = e->loc.line;
    const sema::Type* ct = m_semaTypes.unqualified(m_semaTypes.unref(exprType(e)));
    int slot = newLocal("tmp.cond", byAddr ? m_semaTypes.pointer(ct) : ct, false);
    LValue lv; lv.kind = LValue::Local; lv.local = slot; lv.type = byAddr ? m_semaTypes.pointer(ct) : ct;
    int tb = m_out->newBlock("cond.true"), fb = m_out->newBlock("cond.false"), join = m_out->newBlock("cond.join");
    condition(e->kids[0], tb, fb);
    for (int k = 0; k < 2; ++k) {
        m_block = k == 0 ? tb : fb;
        cxx::Expr* arm = e->kids[static_cast<size_t>(k + 1)];
        size_t tempsBefore = m_temps.size();
        int v;
        if (byAddr && isClassType(ct) && m_semaTypes.unqualified(m_semaTypes.unref(exprType(arm))) != ct) {
            // an operand of another type (a string literal for a std::string result): converted into a temporary
            int conv = newLocal("tmp.cond", ct, true);
            LValue cl; cl.kind = LValue::Local; cl.local = conv; cl.type = ct;
            v = addressOf(cl, line);
            materialize(arm, ct, v, line);
            if (needsDtor(ct)) registerCleanup(conv, ct, false, 1, line);
        } else if (byAddr) {
            LValue al = lvalue(arm);
            v = al.kind == LValue::None ? value(arm) : addressOf(al, line);
            if (al.kind != LValue::None && arm->semaLvalue && !e->semaLvalue && isClassType(ct)) {
                // the other arm is a temporary, so the result is one too: a copy of this lvalue
                int copy = newLocal("tmp.cond", ct, true);
                LValue cl; cl.kind = LValue::Local; cl.local = copy; cl.type = ct;
                int dst = addressOf(cl, line);
                copyConstruct(ct, dst, v, line);
                if (needsDtor(ct)) registerCleanup(copy, ct, false, 1, line);
                v = dst;
            }
        } else v = convert(value(arm), exprType(arm), ct, line);
        store(lv, v, lv.type, line);
        // a temporary the result refers to survives to the end of the full expression, guarded by its
        // flag because only one arm runs; scalar arms destroy their temporaries here
        if (byAddr) for (size_t i = tempsBefore; i < m_temps.size(); ++i) m_temps[i].guarded = true;
        else endFullExpr(line, tempsBefore);
        br(join, line);
    }
    m_block = join;
    return load(lv, lv.type, line);
}

void IrGen::condition(cxx::Expr* e, int trueBlock, int falseBlock)
{
    if (e->kind == cxx::Expr::Paren) { condition(e->kids[0], trueBlock, falseBlock); return; }
    if (e->kind == cxx::Expr::Binary && e->text == "&&" && !e->semaSym) {
        int mid = m_out->newBlock("and.rhs");
        condition(e->kids[0], mid, falseBlock);
        m_block = mid;
        condition(e->kids[1], trueBlock, falseBlock);
        return;
    }
    if (e->kind == cxx::Expr::Binary && e->text == "||" && !e->semaSym) {
        int mid = m_out->newBlock("or.rhs");
        condition(e->kids[0], trueBlock, mid);
        m_block = mid;
        condition(e->kids[1], trueBlock, falseBlock);
        return;
    }
    if (e->kind == cxx::Expr::Unary && e->text == "!" && !e->semaSym) { condition(e->kids[0], falseBlock, trueBlock); return; }
    size_t tempsBefore = m_temps.size();
    int v = toBool(value(e), exprType(e), e->loc.line);
    endFullExpr(e->loc.line, tempsBefore);   // temporaries of this operand die before the branch
    condbr(v, trueBlock, falseBlock, e->loc.line);
}

int IrGen::argument(cxx::Expr* a, const sema::Type* pt, int line)
{
    if (pt && isReference(pt)) {
        const sema::Type* target = m_semaTypes.unref(pt);
        // a class reference bound to another type goes through a converted temporary, even for an lvalue ("pear")
        const sema::Type* at = m_semaTypes.unqualified(m_semaTypes.unref(exprType(a)));
        const sema::Type* tu = m_semaTypes.unqualified(target);
        // a scalar reference bound to a value of another type too (8.5.3/5: const double& from an int lvalue is a
        // converted temporary, not the int's bytes); arrays and functions bind to references of their own type
        bool converted = isClassType(target) ? (at->kind != sema::Type::Class || (at != tu && !(at->decl && tu->decl && isBaseOf(at->decl, tu->decl))))
                                             : (at != tu && !(at->kind == sema::Type::Array && tu->kind == sema::Type::Array) && !(at->kind == sema::Type::Function && tu->kind == sema::Type::Function));
        LValue al;
        // a class prvalue built by a constructor is evaluated once, below (V& bound to D())
        const bool built = isClassType(exprType(a)) && !a->semaLvalue && a->semaSym && a->semaSym->kind == Symbol::Function && a->semaSym->isConstructor;
        if (!converted && !built) al = lvalue(a);
        if (converted || al.kind == LValue::None || (a->semaSym && a->semaSym->kind == Symbol::Function && a->kind == cxx::Expr::Call && !a->semaLvalue && !isClassType(exprType(a)))) {
            // bind a temporary
            if (isClassType(exprType(a)) && al.kind == LValue::None) return convert(value(a), m_semaTypes.pointer(m_semaTypes.unref(exprType(a))), m_semaTypes.pointer(target), line);
            int slot = newLocal("tmp.arg", m_semaTypes.unqualified(target), true);
            LValue tl; tl.kind = LValue::Local; tl.local = slot; tl.type = m_semaTypes.unqualified(target);
            if (!isClassType(target)) store(tl, convert(value(a), exprType(a), target, line), target, line);
            else {
                materialize(a, tl.type, addressOf(tl, line), line);
                if (needsDtor(tl.type)) registerCleanup(slot, tl.type, false, 1, line);
            }
            return addressOf(tl, line);
        }
        int addr = addressOf(al, line);
        return convert(addr, m_semaTypes.pointer(al.type), m_semaTypes.pointer(target), line);
    }
    if (pt && isClassType(pt)) {
        // by value: the callee owns the copy and destroys it (MSVC convention)
        const sema::Type* ct = m_semaTypes.unqualified(m_semaTypes.unref(pt));
        int slot = newLocal("tmp.arg", ct, true);
        LValue tl; tl.kind = LValue::Local; tl.local = slot; tl.type = ct;
        int addr = materialize(a, ct, addressOf(tl, line), line);
        if (const sema::Type* it = regParamType(ct)) { LValue m; m.kind = LValue::Mem; m.addr = addr; m.type = it; return load(m, it, line); }   // its bytes in a register
        return addr;
    }
    if (pt && mpKind(pt) == 3) {
        // a pointer to member in memory: a copy of its own (the callee may assign to it), passed by address, an
        // 8-byte one as an integer, as cl passes them
        const sema::Type* ct = m_semaTypes.unqualified(m_semaTypes.unref(pt));
        const int src = convert(value(a), exprType(a), ct, line);
        int slot = newLocal("tmp.arg", ct, true);
        LValue tl; tl.kind = LValue::Local; tl.local = slot; tl.type = ct;
        const int addr = addressOf(tl, line);
        copyBytes(addr, src, sizeOf(ct), line);
        if (const sema::Type* it = regParamType(ct)) { LValue m; m.kind = LValue::Mem; m.addr = addr; m.type = it; return load(m, it, line); }
        return addr;
    }
    int v = value(a);
    if (pt) v = convert(v, exprType(a), pt, line);
    return v;
}

// the IR type an argument of a parameter of type pt is passed as: the address of a reference's or a by-value
// object's copy, the integer a small object travels as, or the value's own type (a variadic argument's decayed one)
const sema::Type* IrGen::argOperandType(const sema::Type* pt, cxx::Expr* a) const
{
    if (!pt) return m_semaTypes.decay(exprType(a));
    if (isReference(pt)) return m_semaTypes.pointer(m_semaTypes.unref(pt));
    if (isClassType(pt) || mpKind(pt) == 3) { const sema::Type* it = regParamType(pt); return it ? it : m_semaTypes.pointer(pt); }
    return pt;
}

int IrGen::callFunction(Symbol* fn, int thisPtr, int sretAddr, const std::vector<cxx::Expr*>& args, size_t firstArg, bool virtualCall, int line)
{
    need(fn);
    const int vbFlag = m_ctorFlag;   // taken before the arguments, whose own constructions pass 1
    m_ctorFlag = 1;
    const sema::Type* ft = fn->type;
    const sema::Type* rt = ft && ft->kind == sema::Type::Function ? ft->inner : m_semaTypes.basic(sema::Type::Void);
    const bool mpResult = mpKind(rt) == 3 && !isReference(rt);   // cl passes its hidden result pointer before this
    bool sret = (isClassType(rt) || mpResult) && !isReference(rt);
    const sema::Type* retReg = sret ? regReturnType(fn, rt) : 0;
    IrInst in = make(IrInst::Call, retReg ? retReg : sret ? m_semaTypes.basic(sema::Type::Void) : rt, line);
    in.sub = irName(fn);
    const bool vcall = virtualCall && fn->isVirtual && fn->vtableSlot >= 0 && thisPtr >= 0;
    if (thisPtr >= 0) {
        // MSVC's this adjustor: a virtual function first held by a secondary vftable is called with the address of
        // that vfptr (a virtual call loads the vptr there; inside a virtual base it is found through the vbtable); a
        // destructor takes the complete object unless its vfptr lies only in virtual bases
        const sema::Type* cp = m_semaTypes.pointer(m_semaTypes.classType(fn->ownerClass));
        if (vcall) thisPtr = vfptrThis(fn, thisPtr, line);
        else if (fn->thisAdjust != 0) thisPtr = offsetAddress(thisPtr, cp, cp, fn->thisAdjust, line);
        in.args.push_back(IrOperand::temp(thisPtr, oldType(cp)));
        // the destructor's slot holds the deleting destructor: flags 0 destroys without freeing
        if (vcall && fn->isDestructor) in.args.push_back(IrOperand::imm(0, oldType(m_semaTypes.basic(sema::Type::UInt))));
        // MSVC: a constructor of a class with virtual bases takes "most derived" (1: build them) as its last
        // parameter, after this when it is variadic
        if (fn->isConstructor && hasVbases(fn->ownerClass) && ft && ft->variadic) in.args.push_back(IrOperand::imm(vbFlag, oldType(m_semaTypes.basic(sema::Type::Int))));
    }
    int retSlot = -1;
    if (sret) {
        if (sretAddr < 0) { retSlot = newLocal("tmp.ret", m_semaTypes.unqualified(rt), true); LValue tl; tl.kind = LValue::Local; tl.local = retSlot; tl.type = m_semaTypes.unqualified(rt); sretAddr = addressOf(tl, line); }
        if (!retReg) {
            const IrOperand sp = IrOperand::temp(sretAddr, oldType(m_semaTypes.pointer(rt)));
            if (mpResult && thisPtr >= 0) in.args.insert(in.args.begin(), sp); else in.args.push_back(sp);
        }
    }
    for (size_t a = firstArg; a < args.size(); ++a) {
        size_t p = a - firstArg;
        const sema::Type* pt = ft && p < ft->params.size() ? ft->params[p] : 0;
        // a class past the named parameters (variadic) goes as a by-value parameter of its type would
        if (!pt && isClassType(exprType(args[a])) && !isReference(exprType(args[a]))) pt = m_semaTypes.unqualified(m_semaTypes.unref(exprType(args[a])));
        int v = argument(args[a], pt, line);
        in.args.push_back(IrOperand::temp(v, oldType(argOperandType(pt, args[a]))));
    }
    // default arguments
    if (ft) for (size_t p = args.size() - firstArg; p < ft->params.size() && p < fn->params.size(); ++p) {
        cxx::Expr* def = fn->params[p]->init;
        if (!def) break;
        std::vector<cxx::Expr*> one; one.push_back(def);
        int v = argument(def, ft->params[p], line);
        in.args.push_back(IrOperand::temp(v, oldType(argOperandType(ft->params[p], def))));
    }
    if (fn->isConstructor && hasVbases(fn->ownerClass) && thisPtr >= 0 && !(ft && ft->variadic)) in.args.push_back(IrOperand::imm(vbFlag, oldType(m_semaTypes.basic(sema::Type::Int))));
    if (vcall) { in.op = IrInst::VCall; in.extra = fn->vtableSlot; }
    if (isReference(rt)) in.type = oldType(m_semaTypes.pointer(rt->inner));
    int r = emit(in);
    if (retReg) { LValue m; m.kind = LValue::Mem; m.addr = sretAddr; m.type = retReg; store(m, r, retReg, line); }   // the result's bytes came in rax
    if (retSlot >= 0 && needsDtor(rt)) registerCleanup(retSlot, rt, false, 1, line);
    return sret ? sretAddr : r;
}

int IrGen::construct(Symbol* ctor, int objAddr, const std::vector<cxx::Expr*>& args, size_t firstArg, int line)
{
    if (!ctor) return objAddr;
    callFunction(ctor, objAddr, -1, args, firstArg, false, line);
    return objAddr;
}

// ---- special members -----------------------------------------------------------------------------------

Symbol* IrGen::findSpecial(Symbol* cls, char which) const
{
    if (!cls || !cls->members) return 0;
    const std::vector<Symbol*>* v = cls->members->find(which == '~' ? "~" + cls->ctorName() : which == 'a' ? std::string("operator=") : cls->ctorName());
    if (!v) return 0;
    for (size_t i = 0; i < v->size(); ++i) {
        Symbol* f = (*v)[i];
        if (f->kind != Symbol::Function) continue;
        if (which == '~') return f;
        if (which == 'd') { if (f->isConstructor && f->requiredParams == 0) return f; continue; }
        if (f->params.empty() || f->requiredParams > 1) continue;
        const sema::Type* p0 = f->params[0]->type;
        if (!isReference(p0) || m_semaTypes.unqualified(p0->inner) != cls->type) continue;
        if (which == 'c' && f->isConstructor) return f;
        if (which == 'a' && !f->isConstructor) return f;
    }
    return 0;
}

bool IrGen::needsDtor(const sema::Type* t, int depth) const
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (!u || depth > 32) return false;
    if (u->kind == sema::Type::Array) return needsDtor(u->inner, depth + 1);
    if (u->kind != sema::Type::Class || !u->decl) return false;
    Symbol* cls = u->decl;
    if (cls->hasDtor) return true;
    for (size_t b = 0; b < cls->bases.size(); ++b) if (cls->bases[b].cls && needsDtor(cls->bases[b].cls->type, depth + 1)) return true;
    for (size_t f = 0; f < cls->fields.size(); ++f) if (!isReference(cls->fields[f]->type) && needsDtor(cls->fields[f]->type, depth + 1)) return true;
    return false;
}

bool IrGen::needsCopyCtor(const sema::Type* t, int depth) const
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (!u || depth > 32) return false;
    if (u->kind == sema::Type::Array) return needsCopyCtor(u->inner, depth + 1);
    if (u->kind != sema::Type::Class || !u->decl) return false;
    Symbol* cls = u->decl;
    if (cls->hasCopyCtor || cls->isPolymorphic || hasVbases(cls)) return true;   // a copy gets its own class's vptr (vbptrs), not the source's
    for (size_t b = 0; b < cls->bases.size(); ++b) if (cls->bases[b].cls && needsCopyCtor(cls->bases[b].cls->type, depth + 1)) return true;
    for (size_t f = 0; f < cls->fields.size(); ++f) if (!isReference(cls->fields[f]->type) && needsCopyCtor(cls->fields[f]->type, depth + 1)) return true;
    return false;
}

// MSVC x64: a class object of 1, 2, 4 or 8 bytes is passed as an integer of that size (in a register or its
// stack slot) unless copying it needs a constructor; then, like any other size, by address of a copy
const sema::Type* IrGen::regParamType(const sema::Type* t) const
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (u && !isReference(t) && mpKind(u) == 3) return sizeOf(u) == 8 ? m_semaTypes.basic(sema::Type::ULongLong) : 0;   // a pointer to member in memory
    if (!u || u->kind != sema::Type::Class || !u->decl || isReference(t) || needsCopyCtor(u)) return 0;
    if (hasVbases(u->decl)) return 0;
    switch (sizeOf(u)) {
    case 1: return m_semaTypes.basic(sema::Type::UChar);
    case 2: return m_semaTypes.basic(sema::Type::UShort);
    case 4: return m_semaTypes.basic(sema::Type::UInt);
    case 8: return m_semaTypes.basic(sema::Type::ULongLong);
    default: return 0;
    }
}

// a C++03 aggregate-like class (cl's rule): no user-declared constructor, destructor or copy assignment, no
// bases, no virtual functions, no private or protected data members, no reference members, members alike
bool IrGen::podForReturn(const sema::Type* t, int depth) const
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    while (u && u->kind == sema::Type::Array) u = m_semaTypes.unqualified(u->inner);
    if (!u || depth > 32) return false;
    if (u->kind != sema::Type::Class) return false;
    Symbol* cls = u->decl;
    if (!cls || cls->hasUserCtor || cls->hasCopyCtor || cls->hasDtor || cls->hasCopyAssign || cls->isPolymorphic || !cls->bases.empty()) return false;
    for (size_t f = 0; f < cls->fields.size(); ++f) {
        Symbol* m = cls->fields[f];
        if (m->access == cxx::AccessPrivate || m->access == cxx::AccessProtected) return false;
    }
    // members count only through the class's implicit members: a trivial destructor and a trivial,
    // not deleted copy assignment (a const or reference member deletes it)
    return !needsDtor(u) && !needsCopyAssign(u) && !copyAssignDeleted(u, 0);
}

bool IrGen::copyAssignDeleted(const sema::Type* t, int depth) const
{
    const sema::Type* u = m_semaTypes.unref(t);
    if (!u || depth > 32) return false;
    if (isReference(t)) return true;
    while (u->kind == sema::Type::Array) { if (u->isConst) return true; u = u->inner; }
    if (u->isConst) return true;
    u = m_semaTypes.unqualified(u);
    if (u->kind != sema::Type::Class || !u->decl || u->decl->hasCopyAssign) return false;
    for (size_t b = 0; b < u->decl->bases.size(); ++b) if (u->decl->bases[b].cls && copyAssignDeleted(u->decl->bases[b].cls->type, depth + 1)) return true;
    for (size_t f = 0; f < u->decl->fields.size(); ++f) if (copyAssignDeleted(u->decl->fields[f]->type, depth + 1)) return true;
    return false;
}

// the integer type a class result of fn comes back in (rax), or 0 for the hidden result pointer; member
// functions always use the hidden pointer (fn 0: a call through a pointer to function)
const sema::Type* IrGen::regReturnType(Symbol* fn, const sema::Type* rt) const
{
    if (rt && !isReference(rt) && mpKind(rt) == 3) return sizeOf(m_semaTypes.unqualified(rt)) == 8 ? m_semaTypes.basic(sema::Type::ULongLong) : 0;   // also from member functions
    if ((fn && fn->ownerClass && !fn->isStatic) || !rt || isReference(rt) || !isClassType(rt)) return 0;
    if (!podForReturn(rt, 0)) return 0;
    switch (sizeOf(m_semaTypes.unqualified(rt))) {
    case 1: return m_semaTypes.basic(sema::Type::UChar);
    case 2: return m_semaTypes.basic(sema::Type::UShort);
    case 4: return m_semaTypes.basic(sema::Type::UInt);
    case 8: return m_semaTypes.basic(sema::Type::ULongLong);
    default: return 0;
    }
}

bool IrGen::needsCopyAssign(const sema::Type* t, int depth) const
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (!u || depth > 32) return false;
    if (u->kind == sema::Type::Array) return needsCopyAssign(u->inner, depth + 1);
    if (u->kind != sema::Type::Class || !u->decl) return false;
    Symbol* cls = u->decl;
    if (cls->hasCopyAssign || cls->isPolymorphic || hasVbases(cls)) return true;   // an assignment keeps the target's vptr (vbptrs)
    for (size_t b = 0; b < cls->bases.size(); ++b) if (cls->bases[b].cls && needsCopyAssign(cls->bases[b].cls->type, depth + 1)) return true;
    for (size_t f = 0; f < cls->fields.size(); ++f) if (!isReference(cls->fields[f]->type) && needsCopyAssign(cls->fields[f]->type, depth + 1)) return true;
    return false;
}

bool IrGen::needsDefaultCtor(const sema::Type* t, int depth) const
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (!u || depth > 32) return false;
    if (u->kind == sema::Type::Array) return needsDefaultCtor(u->inner, depth + 1);
    if (u->kind != sema::Type::Class || !u->decl) return false;
    Symbol* cls = u->decl;
    if (cls->hasUserCtor || cls->isPolymorphic || hasVbases(cls)) return true;
    for (size_t b = 0; b < cls->bases.size(); ++b) if (cls->bases[b].cls && needsDefaultCtor(cls->bases[b].cls->type, depth + 1)) return true;
    for (size_t f = 0; f < cls->fields.size(); ++f) if (!isReference(cls->fields[f]->type) && needsDefaultCtor(cls->fields[f]->type, depth + 1)) return true;
    return false;
}

void IrGen::callSpecial(Symbol* fn, int thisPtr, int argPtr, int line)
{
    need(fn);
    const sema::Type* pt = m_semaTypes.pointer(m_semaTypes.classType(fn->ownerClass));
    const sema::Type* rt = fn->type && fn->type->kind == sema::Type::Function ? fn->type->inner : m_semaTypes.basic(sema::Type::Void);
    IrInst in = make(IrInst::Call, isReference(rt) ? m_semaTypes.pointer(rt->inner) : rt, line);
    in.sub = irName(fn);
    if (fn->thisAdjust != 0) thisPtr = offsetAddress(thisPtr, pt, pt, fn->thisAdjust, line);
    in.args.push_back(IrOperand::temp(thisPtr, oldType(pt)));
    if (argPtr >= 0) in.args.push_back(IrOperand::temp(argPtr, oldType(pt)));
    if (fn->isConstructor && hasVbases(fn->ownerClass)) in.args.push_back(IrOperand::imm(m_ctorFlag, oldType(m_semaTypes.basic(sema::Type::Int))));   // "most derived", last
    m_ctorFlag = 1;
    emit(in);
}

void IrGen::copyBytes(int dst, int src, int size, int line)
{
    if (size <= 0) return;
    const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
    IrInst c = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
    c.sub = "__copy";
    c.args.push_back(IrOperand::temp(dst, oldType(vp)));
    c.args.push_back(IrOperand::temp(src, oldType(vp)));
    c.args.push_back(IrOperand::imm(size, m_types.intType()));
    emit(c);
}

int IrGen::subobject(int objAddr, const sema::Type* objType, const std::string& name, int offset, const sema::Type* partType, int line)
{
    if (offset == 0 && objType == partType) return objAddr;
    IrInst fa = make(IrInst::AddrField, m_semaTypes.pointer(partType), line);
    fa.sub = name;
    fa.extra = offset;
    fa.args.push_back(IrOperand::temp(objAddr, oldType(m_semaTypes.pointer(objType))));
    return emit(fa);
}

void IrGen::copyConstruct(const sema::Type* t, int dst, int src, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (u->kind == sema::Type::Array && needsCopyCtor(u)) {
        const sema::Type* eu = m_semaTypes.unqualified(u->inner);
        const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
        for (long i = 0; i < u->arraySize; ++i) {
            int d, s;
            IrInst ad = make(IrInst::AddrIndex, m_semaTypes.pointer(eu), line); ad.extra = sizeOf(eu); ad.args.push_back(IrOperand::temp(dst, oldType(m_semaTypes.pointer(eu)))); ad.args.push_back(IrOperand::imm(i, oldType(lng))); d = emit(ad);
            IrInst as = make(IrInst::AddrIndex, m_semaTypes.pointer(eu), line); as.extra = sizeOf(eu); as.args.push_back(IrOperand::temp(src, oldType(m_semaTypes.pointer(eu)))); as.args.push_back(IrOperand::imm(i, oldType(lng))); s = emit(as);
            copyConstruct(eu, d, s, line);
        }
        return;
    }
    Symbol* ctor = u->kind == sema::Type::Class && needsCopyCtor(u) ? findSpecial(u->decl, 'c') : 0;
    if (ctor) callSpecial(ctor, dst, src, line);
    else copyBytes(dst, src, copySize(u), line);
}

void IrGen::copyAssign(const sema::Type* t, int dst, int src, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (u->kind == sema::Type::Array && needsCopyAssign(u)) {
        const sema::Type* eu = m_semaTypes.unqualified(u->inner);
        const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
        for (long i = 0; i < u->arraySize; ++i) {
            int d, s;
            IrInst ad = make(IrInst::AddrIndex, m_semaTypes.pointer(eu), line); ad.extra = sizeOf(eu); ad.args.push_back(IrOperand::temp(dst, oldType(m_semaTypes.pointer(eu)))); ad.args.push_back(IrOperand::imm(i, oldType(lng))); d = emit(ad);
            IrInst as = make(IrInst::AddrIndex, m_semaTypes.pointer(eu), line); as.extra = sizeOf(eu); as.args.push_back(IrOperand::temp(src, oldType(m_semaTypes.pointer(eu)))); as.args.push_back(IrOperand::imm(i, oldType(lng))); s = emit(as);
            copyAssign(eu, d, s, line);
        }
        return;
    }
    Symbol* op = u->kind == sema::Type::Class && needsCopyAssign(u) ? findSpecial(u->decl, 'a') : 0;
    if (op) callSpecial(op, dst, src, line);
    else copyBytes(dst, src, copySize(u), line);
}

void IrGen::constructDefault(const sema::Type* t, int addr, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (u->kind == sema::Type::Array) {
        if (u->arraySize > 0 && needsDefaultCtor(u->inner)) forEachElement(u->inner, addr, constant(u->arraySize, m_semaTypes.basic(sema::Type::LongLong), line), true, line);
        return;
    }
    if (u->kind != sema::Type::Class || !u->decl || !needsDefaultCtor(u)) return;
    Symbol* ctor = findSpecial(u->decl, 'd');
    if (!ctor) { m_notes.push_back("L" + numText(line) + ": no default constructor for " + u->decl->qualifiedName()); return; }
    std::vector<cxx::Expr*> none;
    construct(ctor, addr, none, 0, line);
}

void IrGen::zeroBytes(int addr, int bytes, int line)
{
    if (bytes <= 0) return;
    IrInst z = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
    z.sub = "__zero";
    z.args.push_back(IrOperand::temp(addr, oldType(m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void)))));
    z.args.push_back(IrOperand::imm(0, m_types.intType()));
    z.args.push_back(IrOperand::imm(bytes, m_types.builtin(Type::LongLong)));
    emit(z);
}

// value-initialisation: a class without a user-declared constructor is zero-initialised and then built by its
// implicit constructor when it has one to run (members or bases with constructors, a vptr); a class with one is
// only built by it; anything else (scalars, arrays of either) is zeroed. cl implements C++03's rule, not C++98's
// default-initialisation
void IrGen::valueInit(Symbol* ctor, const sema::Type* t, int addr, bool complete, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    const sema::Type* e = u;
    while (e->kind == sema::Type::Array) e = m_semaTypes.unqualified(e->inner);
    // (a pointer to member is zeroed too, not given its null value: cl does the same)
    const bool userCtor = e->kind == sema::Type::Class && e->decl && e->decl->hasUserCtor;
    if (!userCtor) {
        // a base subobject: its own bytes (an empty base has none); its vbptrs are the complete object's
        if (!complete && u->kind == sema::Type::Class && u->decl && hasVbases(u->decl))
            unsupported(line, "value-initialisation of '" + u->decl->qualifiedName() + "', a base with virtual bases, is not supported yet by --compile6");
        else zeroBytes(addr, complete || u->kind != sema::Type::Class || !u->decl || !u->decl->layoutDone ? sizeOf(u) : u->decl->nvSize, line);
    }
    if (ctor && u->kind == sema::Type::Class && (userCtor || needsDefaultCtor(u))) { std::vector<cxx::Expr*> none; construct(ctor, addr, none, 0, line); }
    else constructDefault(u, addr, line);
}

// constructs (default) or destroys count elements of an array through a loop; count is a long long temp
void IrGen::forEachElement(const sema::Type* elem, int base, int count, bool construct, int line)
{
    const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
    const sema::Type* eu = m_semaTypes.unqualified(elem);
    if (construct ? !needsDefaultCtor(eu) : !needsDtor(eu)) return;
    // when an element's constructor (destructor) throws, the elements before it are destroyed, newest first, as cl
    // does: a cleanup of the elements [0, idx) while the loop runs (none inside a landing pad: terminate)
    const bool rest = m_inPad == 0 && needsDtor(eu);
    int idx = newLocal("tmp.idx", lng, rest);
    LValue il; il.kind = LValue::Local; il.local = idx; il.type = lng;
    store(il, construct ? constant(0, lng, line) : count, lng, line);
    int restCleanup = -1;
    if (rest) {
        int bs = newLocal("tmp.elems", m_semaTypes.pointer(eu), true);
        LValue bl; bl.kind = LValue::Local; bl.local = bs; bl.type = m_semaTypes.pointer(eu);
        store(bl, base, bl.type, line);
        restCleanup = registerCleanup(bs, eu, true, 2, line);
        m_flagged[static_cast<size_t>(restCleanup)].restIndex = idx;
    }
    int cb = m_out->newBlock("elems.cond"), bb = m_out->newBlock("elems.body"), xb = m_out->newBlock("elems.end");
    br(cb, line);
    m_block = cb;
    int cur = load(il, lng, line);
    IrInst c = make(IrInst::Cmp, m_semaTypes.basic(sema::Type::Bool), line);
    c.sub = construct ? "slt" : "sgt";
    c.args.push_back(IrOperand::temp(cur, oldType(lng)));
    if (construct) c.args.push_back(IrOperand::temp(count, oldType(lng))); else c.args.push_back(IrOperand::imm(0, oldType(lng)));
    condbr(emit(c), bb, xb, line);
    m_block = bb;
    int i = load(il, lng, line);
    if (!construct) { i = binary("-", i, lng, constant(1, lng, line), lng, lng, line); store(il, i, lng, line); }
    IrInst ai = make(IrInst::AddrIndex, m_semaTypes.pointer(eu), line);
    ai.extra = sizeOf(eu) > 0 ? sizeOf(eu) : 1;
    ai.args.push_back(IrOperand::temp(base, oldType(m_semaTypes.pointer(eu))));
    ai.args.push_back(IrOperand::temp(i, oldType(lng)));
    int addr = emit(ai);
    if (construct) { constructDefault(eu, addr, line); store(il, binary("+", i, lng, constant(1, lng, line), lng, lng, line), lng, line); }
    else destroy(eu, addr, line, false);
    br(cb, line);
    m_block = xb;
    if (restCleanup >= 0) clearFlag(restCleanup, line);
}

// constructs the object that expression e denotes directly into dst (type t): constructor calls and
// calls returning the class build in place, anything else is copy-constructed from its address
int IrGen::materialize(cxx::Expr* e, const sema::Type* t, int dst, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    cxx::Expr* x = e;
    while (x->kind == cxx::Expr::Paren && !x->kids.empty()) x = x->kids[0];
    Symbol* s = x->semaSym;
    const sema::Type* xt = m_semaTypes.unqualified(m_semaTypes.unref(exprType(x)));
    bool sameClass = xt == u;
    if (sameClass && x->kind == cxx::Expr::FunctionalCast && x->kids.empty()) { valueInit(s && s->kind == Symbol::Function && s->isConstructor ? s : 0, u, dst, true, line); return dst; }
    if (sameClass && s && s->kind == Symbol::Function && s->isConstructor && !x->semaLvalue) {
        if (x->kind == cxx::Expr::Call) return construct(s, dst, x->kids, 1, line);
        if (x->kind == cxx::Expr::FunctionalCast || x->kind == cxx::Expr::Cast || x->kind == cxx::Expr::NamedCast) return construct(s, dst, x->kids, 0, line);
    }
    if (sameClass && x->kind == cxx::Expr::Call && s && s->kind == Symbol::Function && !s->isConstructor && s->type && s->type->kind == sema::Type::Function && !isReference(s->type->inner) && !x->semaLvalue)
        return call(x, dst);
    if (!sameClass && u->kind == sema::Type::Class && u->decl && xt->kind == sema::Type::Class && xt->decl && isBaseOf(xt->decl, u->decl)) {
        // an object of a derived class: its base part is copied (slicing)
        // a prvalue is evaluated once (value: a temporary destroyed at the end of the full-expression)
        int src;
        if (x->semaLvalue) { LValue xl = lvalue(e); src = xl.kind == LValue::None ? value(e) : addressOf(xl, line); }
        else src = value(e);
        copyConstruct(u, dst, convert(src, m_semaTypes.pointer(xt), m_semaTypes.pointer(u), line), line);
        return dst;
    }
    if (!sameClass && u->kind == sema::Type::Class && u->decl) {
        // conversion through a constructor taking the operand's type
        const std::vector<Symbol*>* ctors = u->decl->members ? u->decl->members->find(u->decl->ctorName()) : 0;
        const sema::Type* at = m_semaTypes.unqualified(m_semaTypes.decay(exprType(x)));
        Symbol* chosen = 0;
        if (ctors) for (size_t i = 0; i < ctors->size() && !chosen; ++i) {
            Symbol* c = (*ctors)[i];
            if (c->kind != Symbol::Function || !c->isConstructor || c->params.empty() || c->requiredParams > 1) continue;
            const sema::Type* p = m_semaTypes.unqualified(m_semaTypes.unref(c->params[0]->type));
            if (p == at || (p->isArithmetic() && at->isArithmetic()) || (p->kind == sema::Type::Pointer && at->kind == sema::Type::Pointer)) chosen = c;
        }
        if (chosen) { std::vector<cxx::Expr*> one; one.push_back(e); return construct(chosen, dst, one, 0, line); }
        m_notes.push_back("L" + numText(line) + ": no conversion from " + at->text() + " to " + u->decl->qualifiedName());
        value(e);
        return dst;
    }
    int src = value(e);
    copyConstruct(u, dst, src, line);
    return dst;
}

void IrGen::destroy(const sema::Type* t, int objAddr, int line, bool viaVtable, bool complete)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (u->kind == sema::Type::Array) {
        if (u->arraySize > 0 && needsDtor(u->inner)) forEachElement(u->inner, objAddr, constant(u->arraySize, m_semaTypes.basic(sema::Type::LongLong), line), false, line);
        return;
    }
    if (u->kind != sema::Type::Class || !u->decl) return;
    Symbol* dtor = findSpecial(u->decl, '~');
    if (!dtor) return;
    bool virtualCall = viaVtable && dtor->isVirtual;
    if (!needsDtor(u) && !virtualCall) return;
    std::vector<cxx::Expr*> none;
    // a complete object with virtual bases: cl's vbase destructor (??_D) destroys them after ~D
    if (complete && !virtualCall && hasVbases(u->decl)) { callSpecial(vbaseDestructor(u->decl), objAddr, -1, line); return; }
    callFunction(dtor, objAddr, -1, none, 0, virtualCall, line);
}

// ---- lifetimes ------------------------------------------------------------------------------------------

// mode 0: object of the innermost scope, 1: temporary of the current full expression, 2: destroyed only
// when an exception unwinds (members already built by a constructor whose body may still throw)
int IrGen::registerCleanup(int slot, const sema::Type* t, bool viaPointer, int mode, int line)
{
    Cleanup c;
    c.slot = slot;
    c.type = m_semaTypes.unqualified(m_semaTypes.unref(t));
    c.viaPointer = viaPointer;
    c.unwindOnly = mode == 2;
    const sema::Type* b = m_semaTypes.basic(sema::Type::Bool);
    c.flag = newLocal("flag." + m_out->locals[static_cast<size_t>(slot)].name, b, true);
    LValue fl; fl.kind = LValue::Local; fl.local = c.flag; fl.type = b;
    store(fl, constant(1, b, line), b, line);
    m_flagged.push_back(c);
    if (mode == 0) m_cleanups.push_back(c);
    else if (mode == 1) m_temps.push_back(c);
    return static_cast<int>(m_flagged.size() - 1);
}

void IrGen::runCleanup(const Cleanup& c, bool guarded, int line)
{
    const sema::Type* b = m_semaTypes.basic(sema::Type::Bool);
    LValue fl; fl.kind = LValue::Local; fl.local = c.flag; fl.type = b;
    int next = -1;
    if (guarded) {
        int doBlock = m_out->newBlock("dtor");
        next = m_out->newBlock("dtor.next");
        condbr(toBool(load(fl, b, line), b, line), doBlock, next, line);
        m_block = doBlock;
    }
    if (!c.abortGuard.empty()) {
        // a static local's initializer threw: the guard goes back to "not built", so a later call retries
        LValue gl; gl.kind = LValue::Global; gl.global = c.abortGuard; gl.type = c.type;
        IrInst ab = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
        ab.sub = "_Init_thread_abort";
        ab.args.push_back(IrOperand::temp(addressOf(gl, line), oldType(m_semaTypes.pointer(c.type))));
        emit(ab);
        store(fl, constant(0, b, line), b, line);
        if (guarded) { br(next, line); m_block = next; }
        return;
    }
    // the flag is cleared first: a destructor that throws has ended the object's lifetime, the unwind handler
    // must not destroy it again
    store(fl, constant(0, b, line), b, line);
    const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
    if (c.freeKind) {
        // the memory of a new-expression whose constructor threw (a delete whose destructor throws frees nothing, as cl)
        const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
        LValue pl; pl.kind = LValue::Local; pl.local = c.slot; pl.type = vp;
        const int p = load(pl, vp, line);
        if (c.freeKind == 2) {
            // the global operator delete[] too gets the size allocated, as cl
            int count = -1;
            if (c.countSlot >= 0) { LValue nl; nl.kind = LValue::Local; nl.local = c.countSlot; nl.type = lng; count = load(nl, lng, line); }
            freeArray(c.freeCls, p, count, c.freeSize, line, !c.noCookie);
        } else if (!c.freeCls || !freeObject(c.freeCls, p, line)) freeGlobal(p, c.freeSize, line);
        if (guarded) { br(next, line); m_block = next; }
        return;
    }
    int addr;
    if (c.viaPointer) { LValue pl; pl.kind = LValue::Local; pl.local = c.slot; pl.type = m_semaTypes.pointer(c.type); addr = load(pl, pl.type, line); }
    else { LValue ol; ol.kind = LValue::Local; ol.local = c.slot; ol.type = c.type; addr = addressOf(ol, line); }
    if (c.restIndex >= 0) {
        // the elements of an array built (or not yet destroyed) when an element's constructor (destructor) threw
        LValue il; il.kind = LValue::Local; il.local = c.restIndex; il.type = lng;
        forEachElement(c.type, addr, load(il, lng, line), false, line);
    } else destroy(c.type, addr, line, false, !c.baseSubobject);
    if (guarded) { br(next, line); m_block = next; }
}

void IrGen::openScope()
{
    m_scopes.push_back(m_cleanups.size());
    m_scopeIds.push_back(m_scopeSerial++);
}

IrGen::JumpState IrGen::jumpState(int block, int line) const
{
    JumpState j;
    j.block = block; j.line = line; j.ids = m_scopeIds; j.scopes = m_scopes; j.cleanups = m_cleanups; j.tryStack = m_tryStack;
    return j;
}

// goto: destroys the objects of the scopes left (innermost first) and leaves the try regions, as break and return
// do, from the state at the goto (m_cleanups, m_tryStack hold it). A jump into a scope past an object with a
// destructor, or into a try block or a handler, is ill-formed (C++98 6.7/3, 15/2): refused
void IrGen::gotoExit(const JumpState& from, const JumpState& to, const std::string& label)
{
    size_t k = 0;
    while (k < from.ids.size() && k < to.ids.size() && from.ids[k] == to.ids[k]) ++k;
    const size_t keepFrom = k < from.scopes.size() ? from.scopes[k] : from.cleanups.size();
    const size_t keepTo = k < to.scopes.size() ? to.scopes[k] : to.cleanups.size();
    size_t t = 0;
    while (t < from.tryStack.size() && t < to.tryStack.size() && from.tryStack[t] == to.tryStack[t]) ++t;
    if (keepTo > keepFrom) unsupported(from.line, "goto " + label + " jumps past the initialisation of an object with a destructor (ill-formed)");
    else if (t < to.tryStack.size()) unsupported(from.line, "goto " + label + " jumps into a try block or a handler (ill-formed)");
    leaveScopes(keepTo < keepFrom ? keepTo : keepFrom, from.line);
    leaveTryRegions(t, from.line);
    br(labelBlock(label), from.line);
}

void IrGen::closeScope(int line)
{
    size_t mark = m_scopes.back();
    m_scopes.pop_back();
    m_scopeIds.pop_back();
    if (!terminated()) for (size_t i = m_cleanups.size(); i > mark; --i) runCleanup(m_cleanups[i - 1], m_cleanups[i - 1].guarded, line);
    m_cleanups.erase(m_cleanups.begin() + static_cast<std::ptrdiff_t>(mark), m_cleanups.end());
}

void IrGen::leaveScopes(size_t depth, int line)
{
    for (size_t i = m_cleanups.size(); i > depth; --i) runCleanup(m_cleanups[i - 1], m_cleanups[i - 1].guarded, line);
}

void IrGen::endFullExpr(int line, size_t from)
{
    if (!terminated()) for (size_t i = m_temps.size(); i > from; --i) runCleanup(m_temps[i - 1], m_temps[i - 1].guarded, line);
    m_temps.erase(m_temps.begin() + static_cast<std::ptrdiff_t>(from), m_temps.end());
}

// temporaries created since index 'from' live as long as the enclosing scope (bound to a reference); one
// built by a single arm of a conditional stays guarded by its flag
void IrGen::keepTemporaries(size_t from)
{
    for (size_t i = from; i < m_temps.size(); ++i) m_cleanups.push_back(m_temps[i]);
    m_temps.erase(m_temps.begin() + static_cast<std::ptrdiff_t>(from), m_temps.end());
}

// at a handler entry: the objects built inside the region (flagged from 'mark' on) that are still alive; the
// destructors run inside a region of their own: an exception that leaves one while the stack unwinds calls
// terminate (C++98 15.5.1), as cl does
void IrGen::handlerCleanups(size_t mark, int line)
{
    if (m_flagged.size() <= mark) return;
    int term = -1;
    const int tid = beginTerminateRegion(term, line);
    ++m_inPad;   // no cleanups are registered meanwhile: m_flagged is being walked
    // the destructors run while the exception is being unwound: std::uncaught_exception() is true in them
    { IrInst u = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line); u.sub = "cppparse_unwind_enter"; emit(u); }
    for (size_t i = m_flagged.size(); i > mark; --i) runCleanup(m_flagged[i - 1], true, line);
    { IrInst u = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line); u.sub = "cppparse_unwind_leave"; emit(u); }
    --m_inPad;
    endTerminateRegion(tid, term, line);
}

// a region whose handler calls terminate (code that runs while an exception is being handled: the destructors
// of a landing pad, the copy of a by-value catch parameter); 'term' receives its handler block
int IrGen::beginTerminateRegion(int& term, int line)
{
    const int tid = m_tryCount++;
    term = m_out->newBlock("unwind.terminate");
    const int body = m_out->newBlock("unwind.cleanups");
    IrInst tb = make(IrInst::TryBegin, 0, line);
    tb.extra = tid;
    tb.args.push_back(IrOperand::block(term));
    emit(tb);
    br(body, line);
    m_block = body;
    m_tryStack.push_back(tid);
    return tid;
}

void IrGen::endTerminateRegion(int tid, int term, int line)
{
    m_tryStack.pop_back();
    if (!terminated()) { IrInst te = make(IrInst::TryEnd, 0, line); te.extra = tid; emit(te); }
    const int after = m_block;
    m_block = term;
    IrInst cp = make(IrInst::CatchParam, m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void)), line);
    cp.sub = "...";
    emit(cp);
    IrInst t = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
    t.sub = "cppparse_terminate";
    emit(t);
    emit(make(IrInst::Throw, m_semaTypes.basic(sema::Type::Void), line));
    m_block = after;
}

// the memory a new-expression allocated, freed through the matching operator delete when the constructor (of an
// element) throws; 'ptr' is the address the operator new returned (an array's cookie), 'count' its element count
int IrGen::registerFree(int ptr, Symbol* cls, bool array, int count, int elemSize, int line)
{
    const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
    const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
    int slot = newLocal("tmp.alloc", vp, true);
    LValue pl; pl.kind = LValue::Local; pl.local = slot; pl.type = vp;
    store(pl, ptr, vp, line);
    int countSlot = -1;
    if (array && count >= 0) {
        countSlot = newLocal("tmp.count", lng, true);
        LValue nl; nl.kind = LValue::Local; nl.local = countSlot; nl.type = lng;
        store(nl, count, lng, line);
    }
    const int k = registerCleanup(slot, vp, true, 2, line);
    Cleanup& c = m_flagged[static_cast<size_t>(k)];
    c.freeKind = array ? 2 : 1;
    c.freeCls = cls;
    c.countSlot = countSlot;
    c.freeSize = elemSize;
    if (array && countSlot < 0) c.freeCls = 0;
    return k;
}

void IrGen::clearFlag(int index, int line)
{
    const sema::Type* b = m_semaTypes.basic(sema::Type::Bool);
    LValue fl; fl.kind = LValue::Local; fl.local = m_flagged[static_cast<size_t>(index)].flag; fl.type = b;
    store(fl, constant(0, b, line), b, line);
}

void IrGen::returnSequence(int line)
{
    leaveScopes(0, line);
    if (m_fn->isDestructor) destructorCleanup(m_fn);
    if (m_fn->isConstructor) {
        const sema::Type* b = m_semaTypes.basic(sema::Type::Bool);
        for (size_t i = 0; i < m_flagged.size(); ++i) if (m_flagged[i].unwindOnly) { LValue fl; fl.kind = LValue::Local; fl.local = m_flagged[i].flag; fl.type = b; store(fl, constant(0, b, line), b, line); }
    }
    leaveTryRegions(0, line);
}

// entry block: every flag cleared, then the catch-all region around the body when the function owns
// objects with destructors; its handler destroys what is still constructed and rethrows
void IrGen::finishFunction(int line)
{
    m_block = 0;
    const sema::Type* b = m_semaTypes.basic(sema::Type::Bool);
    for (size_t i = 0; i < m_flagged.size(); ++i) { LValue fl; fl.kind = LValue::Local; fl.local = m_flagged[i].flag; fl.type = b; store(fl, constant(0, b, line), b, line); }
    if (!m_flagged.empty()) {
        int handler = m_out->newBlock("unwind");
        IrInst tb = make(IrInst::TryBegin, 0, line);
        tb.extra = m_unwindId;
        tb.args.push_back(IrOperand::block(handler));
        emit(tb);
        br(m_bodyBlock, line);
        m_block = handler;
        IrInst cp = make(IrInst::CatchParam, m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void)), line);
        cp.sub = "...";
        emit(cp);
        handlerCleanups(0, line);
        emit(make(IrInst::Throw, m_semaTypes.basic(sema::Type::Void), line));
        m_out->hasTry = true;
    } else {
        br(m_bodyBlock, line);
        for (size_t bi = 0; bi < m_out->blocks.size(); ++bi)
            for (size_t i = 0; i < m_out->blocks[bi].insts.size(); ++i) {
                IrInst& in = m_out->blocks[bi].insts[i];
                if (in.op == IrInst::TryEnd && in.extra == m_unwindId) { in.op = IrInst::Nop; in.args.clear(); }
            }
    }
    m_out->computeCfg();
    m_outList->push_back(m_out);
    ++m_generated;
}

// a member declared in a base class is called with the address of that base subobject
int IrGen::adjustThis(Symbol* fn, int objAddr, const sema::Type* objType, int line)
{
    const sema::Type* ot = m_semaTypes.unqualified(m_semaTypes.unref(objType));
    if (ot->kind != sema::Type::Class || !ot->decl || !fn->ownerClass || ot->decl == fn->ownerClass) return objAddr;
    return convert(objAddr, m_semaTypes.pointer(ot), m_semaTypes.pointer(m_semaTypes.classType(fn->ownerClass)), line);
}

// the call an operator expression resolved to (e->semaSym): the raw result, an address for a reference result
int IrGen::operatorCall(cxx::Expr* e)
{
    const int line = e->loc.line;
    Symbol* fn = e->semaSym;
    std::vector<cxx::Expr*> args;
    int thisPtr = -1;
    switch (e->kind) {
    case cxx::Expr::Unary: case cxx::Expr::PostIncDec:
        if (fn->ownerClass) thisPtr = adjustThis(fn, addressOf(lvalue(e->kids[0]), line), exprType(e->kids[0]), line); else args.push_back(e->kids[0]);
        if (e->kind == cxx::Expr::PostIncDec && e->kids.size() > 1) args.push_back(e->kids[1]);   // the int of operator++(int)
        break;
    case cxx::Expr::Binary: case cxx::Expr::Assign: case cxx::Expr::Index: case cxx::Expr::Comma:
        if (fn->ownerClass) { thisPtr = adjustThis(fn, addressOf(lvalue(e->kids[0]), line), exprType(e->kids[0]), line); args.push_back(e->kids[1]); }
        else { args.push_back(e->kids[0]); args.push_back(e->kids[1]); }
        break;
    default:
        break;
    }
    return callFunction(fn, thisPtr, -1, args, 0, true, line);
}

// a call whose result is a scalar reference yields its address: loaded where a value is wanted
// cur +/- 1 as ++ and -- compute it (5.2.6, 5.3.2: like += 1): a pointer steps, an arithmetic operand is promoted
// (its usual arithmetic conversion with int) and the result converted back to its type, so a narrow type wraps
// (unsigned char 255 -> 0, short 32767 -> -32768) at every optimisation level
int IrGen::incremented(int cur, const sema::Type* ot, bool up, int line)
{
    const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
    const sema::Type* ou = m_semaTypes.unqualified(ot);
    if (ou->kind == sema::Type::Pointer) return binary(up ? "+" : "-", cur, ot, constant(1, it, line), it, ot, line);
    const sema::Type* common = ou->isArithmetic() ? m_semaTypes.arithmeticResult(ou, it) : ou;
    int nv = binary(up ? "+" : "-", cur, ot, constant(1, common, line), common, common, line);
    return convert(nv, common, ot, line);
}

int IrGen::rvalue(int r, cxx::Expr* e, int line)
{
    const sema::Type* rt = exprType(e);
    if (!isReference(rt)) return r;
    const sema::Type* u = m_semaTypes.unref(rt);
    if (byAddress(u)) return r;
    LValue m; m.kind = LValue::Mem; m.addr = r; m.type = u;
    return load(m, u, line);
}

// a cast whose operand is converted by a conversion function (e->semaSym): the call on the operand, its result
// read through a reference it returns, then converted on to the cast's type ((bool)stream through operator void*,
// (long)obj through operator int, (double)obj through operator double); a class result stays by address
int IrGen::conversionCall(cxx::Expr* e, int line)
{
    Symbol* fn = e->semaSym;
    std::vector<cxx::Expr*> none;
    int obj = adjustThis(fn, addressOf(lvalue(e->kids[0]), line), exprType(e->kids[0]), line);
    int r = callFunction(fn, obj, -1, none, 0, true, line);
    const sema::Type* rt = fn->type && fn->type->kind == sema::Type::Function && fn->type->inner ? fn->type->inner : exprType(e);
    if (isReference(rt)) {
        const sema::Type* u = m_semaTypes.unref(rt);
        if (byAddress(u)) return r;
        LValue m; m.kind = LValue::Mem; m.addr = r; m.type = u;
        r = load(m, u, line);
    }
    const sema::Type* to = exprType(e);
    if (isReference(to) || isClassType(to) || byAddress(m_semaTypes.unref(rt))) return r;
    return convert(r, rt, to, line);
}

int IrGen::call(cxx::Expr* e, int sretAddr)
{
    const int line = e->loc.line;
    cxx::Expr* callee = e->kids[0];
    Symbol* fn = e->semaSym;
    const sema::Type* rt = exprType(e);
    if (!fn && callee->kind == cxx::Expr::Member && !callee->name.last().empty() && callee->name.last()[0] == '~') {
        // pseudo-destructor call on a scalar: evaluate the object, do nothing
        if (!callee->kids.empty()) value(callee->kids[0]);
        return constant(0, m_semaTypes.basic(sema::Type::Int), line);
    }
    if (!fn && callee->kind == cxx::Expr::Binary && (callee->text == ".*" || callee->text == "->*") && !callee->semaSym && callee->kids.size() == 2
        && m_semaTypes.unqualified(m_semaTypes.unref(exprType(callee->kids[1])))->kind == sema::Type::MemberPointer)
        return memberPointerCall(e, callee, sretAddr);
    if (!fn || fn->kind != Symbol::Function) {
        // function pointer or undeclared function
        IrInst in = make(IrInst::Call, rt, line);
        // (*fp)(x), (fp)(x): the pointer itself is called
        while ((callee->kind == cxx::Expr::Paren || (callee->kind == cxx::Expr::Unary && callee->text == "*")) && callee->kids.size() == 1 && !callee->semaSym) {
            const sema::Type* kt = m_semaTypes.unref(exprType(callee->kids[0]));
            if (!kt || !((kt->kind == sema::Type::Pointer && kt->inner->kind == sema::Type::Function) || kt->kind == sema::Type::Function || callee->kind == cxx::Expr::Paren)) break;
            callee = callee->kids[0];
        }
        const sema::Type* ct = m_semaTypes.unref(exprType(callee));
        // "(*fp)(x)": the dereferenced pointer is the address called
        while (ct->kind == sema::Type::Function && callee->kind == cxx::Expr::Unary && callee->text == "*" && callee->kids.size() == 1) {
            callee = callee->kids[0];
            ct = m_semaTypes.unref(exprType(callee));
        }
        if (ct->kind == sema::Type::Function && callee->semaSym && callee->semaSym->kind == Symbol::Function) ct = m_semaTypes.pointer(ct);   // "(*f)(x)", f a function
        if (ct->kind == sema::Type::Pointer && ct->inner->kind == sema::Type::Function) {
            int fp = value(callee);
            in.args.push_back(IrOperand::temp(fp, oldType(ct)));
            in.sub = "(indirect)";
        } else {
            std::string name = callee->kind == cxx::Expr::Name ? callee->name.last() : callee->kind == cxx::Expr::Member ? callee->name.last() : "?";
            if (callee->kind == cxx::Expr::Member) { cxx::Expr* obj = callee->kids[0]; in.args.push_back(IrOperand::temp(callee->arrowMember ? value(obj) : addressOf(lvalue(obj), line), oldType(m_semaTypes.decay(exprType(obj))))); }
            in.sub = name + "?";
        }
        const sema::Type* pft = ct->kind == sema::Type::Pointer && ct->inner->kind == sema::Type::Function ? ct->inner : 0;
        for (size_t a = 1; a < e->kids.size(); ++a) {
            const sema::Type* pt = pft && a - 1 < pft->params.size() ? pft->params[a - 1] : 0;
            // a parameter of the pointer's function type converts the argument as a direct call does: a reference
            // to a base binds the base part (adjusted), a class is copied, a scalar converted; the variadic rest as is
            if (pt && pt->kind != sema::Type::Error && pt->kind != sema::Type::Dependent) { int v = argument(e->kids[a], pt, line); in.args.push_back(IrOperand::temp(v, oldType(argOperandType(pt, e->kids[a])))); continue; }
            int v = value(e->kids[a]); in.args.push_back(IrOperand::temp(v, oldType(m_semaTypes.decay(exprType(e->kids[a])))));
        }
        if (!rt || rt->kind == sema::Type::Error || rt->kind == sema::Type::Dependent) in.type = m_types.builtin(Type::Long);
        if (pft && (isClassType(rt) || mpKind(rt) == 3) && !isReference(rt)) {
            // a class result: the hidden result pointer after the callee, or the bytes in rax
            const sema::Type* ru = m_semaTypes.unqualified(m_semaTypes.unref(rt));
            int slot = sretAddr >= 0 ? -1 : newLocal("tmp.ret", ru, true);
            int addr = sretAddr;
            if (slot >= 0) { LValue tl; tl.kind = LValue::Local; tl.local = slot; tl.type = ru; addr = addressOf(tl, line); }
            const sema::Type* retReg = regReturnType(0, ru);
            if (retReg) in.type = oldType(retReg);
            else { in.type = oldType(m_semaTypes.basic(sema::Type::Void)); in.args.insert(in.args.begin() + 1, IrOperand::temp(addr, oldType(m_semaTypes.pointer(ru)))); }
            int r = emit(in);
            if (retReg) { LValue m; m.kind = LValue::Mem; m.addr = addr; m.type = retReg; store(m, r, retReg, line); }
            if (slot >= 0 && needsDtor(ru)) registerCleanup(slot, ru, false, 1, line);
            return addr;
        }
        return emit(in);
    }
    int thisPtr = -1;
    bool virtualCall = false;
    if (fn->isConstructor) {
        // T(args) as a call: construct into the requested object, else a temporary
        if (sretAddr >= 0) return construct(fn, sretAddr, e->kids, 1, line);
        const sema::Type* ct = m_semaTypes.classType(fn->ownerClass);
        int slot = newLocal("tmp." + fn->ownerClass->name, ct, true);
        LValue tl; tl.kind = LValue::Local; tl.local = slot; tl.type = ct;
        int r = construct(fn, addressOf(tl, line), e->kids, 1, line);
        if (needsDtor(ct)) registerCleanup(slot, ct, false, 1, line);
        return r;
    }
    // "t(c, r)" with t an object: operator() on t, also inside a member function of t's own class, where
    // a plain name would otherwise call through the implicit this
    const bool objectCall = fn->isOperator && fn->name == "operator()" && callee->kind == cxx::Expr::Name && callee->name.last() != "operator()";
    if (callee->kind == cxx::Expr::Member && fn->ownerClass && !fn->isStatic && fn->isOperator && fn->name == "operator()" &&
        callee->semaSym && callee->semaSym->kind == Symbol::Variable) {
        // "h.f(1)", "p->f(1)" with f a data member of class type: operator() on the member itself
        thisPtr = addressOf(lvalue(callee), line);
        const sema::Type* memT = m_semaTypes.unqualified(m_semaTypes.unref(exprType(callee)));
        if (memT->kind == sema::Type::Class && memT->decl && memT->decl != fn->ownerClass) thisPtr = convert(thisPtr, m_semaTypes.pointer(memT), m_semaTypes.pointer(m_semaTypes.classType(fn->ownerClass)), line);
        virtualCall = true;
    } else if (callee->kind == cxx::Expr::Member && fn->ownerClass && !fn->isStatic) {
        cxx::Expr* obj = callee->kids[0];
        const sema::Type* objT = m_semaTypes.unref(exprType(obj));
        if (callee->arrowMember) {
            thisPtr = value(obj);
            if (objT->kind == sema::Type::Pointer) objT = objT->inner;
        } else { LValue ol = lvalue(obj); thisPtr = ol.kind == LValue::None ? value(obj) : load(ol, objT, line); }
        objT = m_semaTypes.unqualified(objT);
        Symbol* naming = objT->kind == sema::Type::Class ? namingBase(callee, objT->decl) : 0;
        if (naming && naming != fn->ownerClass) {
            // "d.C::f()": through the named base first
            thisPtr = convert(thisPtr, m_semaTypes.pointer(objT), m_semaTypes.pointer(m_semaTypes.classType(naming)), line);
            objT = m_semaTypes.classType(naming);
        }
        if (objT->kind == sema::Type::Class && objT->decl && objT->decl != fn->ownerClass) thisPtr = convert(thisPtr, m_semaTypes.pointer(objT), m_semaTypes.pointer(m_semaTypes.classType(fn->ownerClass)), line);
        virtualCall = callee->name.parts.size() == 1;
    } else if (callee->kind != cxx::Expr::Member && !objectCall && fn->ownerClass && !fn->isStatic && m_out->hasThis) {
        thisPtr = thisTemp(line);
        if (m_fn->ownerClass != fn->ownerClass) thisPtr = convert(thisPtr, m_semaTypes.pointer(m_semaTypes.classType(m_fn->ownerClass)), m_semaTypes.pointer(m_semaTypes.classType(fn->ownerClass)), line);
        virtualCall = callee->kind == cxx::Expr::Name && callee->name.parts.size() == 1;
    } else if (callee->kind != cxx::Expr::Name && callee->kind != cxx::Expr::Member && fn->ownerClass && !fn->isStatic) {
        // operator() on an object
        thisPtr = addressOf(lvalue(callee), line);
    }
    size_t firstArg = 1;
    if (fn->isOperator && fn->name == "operator()" && callee->kind == cxx::Expr::Name && thisPtr < 0) { LValue ol = lvalue(callee); thisPtr = ol.kind == LValue::None ? value(callee) : addressOf(ol, line); }
    return callFunction(fn, thisPtr, sretAddr, e->kids, firstArg, virtualCall, line);
}

int IrGen::value(cxx::Expr* e)
{
    if (!e) return -1;
    const int line = e->loc.line;
    const sema::Type* t = m_semaTypes.unref(exprType(e));
    // a cast of a floating constant goes through convert(), which keeps its untruncated value for a floating
    // type it is converted to next
    if (e->semaConst && !floatingCast(e) && e->kind != cxx::Expr::Call && e->kind != cxx::Expr::Assign && (t->isIntegral() || t->kind == sema::Type::NullPtr || t->isPointer()) && e->kind != cxx::Expr::StringLit)
        return constant(e->semaValue, t->kind == sema::Type::NullPtr ? m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void)) : t, line);
    if (foldableFloating(e)) {
        // a floating constant expression, folded as cl folds it: "2.0 * 1.5e9", "(float)1e10", a const double
        double v = 0;
        if (foldArith(e, v, 0)) { IrInst in = make(IrInst::FConst, t, line); in.args.push_back(IrOperand::fimm(v, oldType(t))); return emit(in); }
    }

    switch (e->kind) {
    case cxx::Expr::IntLit:
        // a 64-bit literal outside the 32-bit sign-extended immediates: put together from pieces
        if (!e->semaConst && sizeOf(t) == 8) return bitConstant(sema::integerLiteralValue(e->text, 0), t, line);
        return constant(e->semaValue, t, line);
    case cxx::Expr::CharLit: case cxx::Expr::BoolLit: return constant(e->semaValue, t, line);
    case cxx::Expr::FloatLit: {
        // a float literal's constant holds its float value, rounded once from the decimal (the optimiser folds
        // conversions of it)
        const double v = floatingLiteralValue(e->text, t->kind == sema::Type::Float);
        IrInst in = make(IrInst::FConst, t, line); in.args.push_back(IrOperand::fimm(v, oldType(t))); return emit(in);
    }
    case cxx::Expr::StringLit: {
        IrInst in = make(IrInst::StrConst, 0, line); in.type = m_types.charPtr(); in.args.push_back(IrOperand::str(e->text, m_types.charPtr())); return emit(in);
    }
    case cxx::Expr::This: return m_out->hasThis ? thisTemp(line) : constant(0, t, line);
    case cxx::Expr::Name: {
        Symbol* s = e->semaSym;
        if (s && (s->kind == Symbol::Function || s->kind == Symbol::FunctionTemplate)) { need(s); IrInst in = make(IrInst::AddrGlobal, 0, line); in.type = m_types.function(oldType(s->type ? s->type->inner : 0)); in.args.push_back(IrOperand::global(irName(s), in.type)); return emit(in); }
        if (s && s->kind == Symbol::Enumerator) return constant(s->constValue, t, line);
        LValue lv = lvalue(e);
        if (lv.kind == LValue::None) { m_notes.push_back("L" + numText(line) + ": unresolved name " + e->name.text() + " read as 0"); return constant(0, t, line); }
        return load(lv, exprType(e), line);
    }
    case cxx::Expr::Member: {
        Symbol* s = e->semaSym;
        if (s && s->kind == Symbol::Enumerator) return constant(s->constValue, t, line);
        LValue lv = lvalue(e);
        if (lv.kind == LValue::None) { if (!e->kids.empty()) value(e->kids[0]); m_notes.push_back("L" + numText(line) + ": unresolved member " + e->name.text() + " read as 0"); return constant(0, t, line); }
        return load(lv, exprType(e), line);
    }
    case cxx::Expr::Index: {
        if (e->semaSym && e->semaSym->kind == Symbol::Function) return rvalue(operatorCall(e), e, line);
        LValue lv = lvalue(e);
        if (lv.kind == LValue::None) return constant(0, t, line);
        return load(lv, exprType(e), line);
    }
    case cxx::Expr::Call: return rvalue(call(e), e, line);
    case cxx::Expr::FunctionalCast: {
        const sema::Type* to = exprType(e);
        if (isClassType(to) && ((e->semaSym && e->semaSym->isConstructor) || e->kids.empty())) {
            Symbol* ctor = e->semaSym && e->semaSym->isConstructor ? e->semaSym : 0;
            int slot = newLocal("tmp." + m_semaTypes.unqualified(m_semaTypes.unref(to))->decl->name, m_semaTypes.unqualified(to), true);
            LValue tl; tl.kind = LValue::Local; tl.local = slot; tl.type = m_semaTypes.unqualified(to);
            int r = addressOf(tl, line);
            if (e->kids.empty()) valueInit(ctor, tl.type, r, true, line);
            else r = construct(ctor, r, e->kids, 0, line);
            if (needsDtor(tl.type)) registerCleanup(slot, tl.type, false, 1, line);
            return r;
        }
        if (e->semaSym && e->semaSym->isConversion) return conversionCall(e, line);
        if (e->kids.empty()) return constant(0, to, line);
        return convert(value(e->kids[0]), exprType(e->kids[0]), to, line);
    }
    case cxx::Expr::Cast: case cxx::Expr::NamedCast: {
        if (e->kids.empty()) return constant(0, t, line);
        if (e->kind == cxx::Expr::NamedCast && e->text == "dynamic_cast") { int r = -1; if (dynamicCast(e, line, r)) return r; }
        if (e->semaSym && e->semaSym->kind == Symbol::Function && e->semaSym->isConversion) return conversionCall(e, line);
        if (e->semaSym && e->semaSym->kind == Symbol::Function && e->semaSym->isConstructor) {
            int slot = newLocal("tmp." + e->semaSym->ownerClass->name, m_semaTypes.unqualified(t), true);
            LValue tl; tl.kind = LValue::Local; tl.local = slot; tl.type = m_semaTypes.unqualified(t);
            int r = construct(e->semaSym, addressOf(tl, line), e->kids, 0, line);
            if (needsDtor(tl.type)) registerCleanup(slot, tl.type, false, 1, line);
            return r;
        }
        const bool reinterpret = e->kind == cxx::Expr::NamedCast && e->text == "reinterpret_cast";
        if (isReference(exprType(e))) { LValue inner = lvalue(e->kids[0]); if (inner.kind != LValue::None) return reinterpret ? addressOf(inner, line) : convert(addressOf(inner, line), m_semaTypes.pointer(inner.type), m_semaTypes.pointer(t), line); }
        if (reinterpret && t->isPointer() && m_semaTypes.decay(exprType(e->kids[0]))->isPointer()) return value(e->kids[0]);   // the same address
        return convert(value(e->kids[0]), exprType(e->kids[0]), t, line);
    }
    case cxx::Expr::Paren: return value(e->kids[0]);
    case cxx::Expr::Unary: {
        const std::string& op = e->text;
        if (e->semaSym && e->semaSym->kind == Symbol::Function) return rvalue(operatorCall(e), e, line);
        if (op == "&") {
            cxx::Expr* o = e->kids[0];
            // &C::m: a pointer to member (sema typed it so)
            if (t->kind == sema::Type::MemberPointer && isNonStaticMember(o->semaSym)) return mpAddressOf(o->semaSym, t, line);
            // &f names the function; for an overloaded operator ("&v[0]") semaSym is the operator called
            if ((o->kind == cxx::Expr::Name || o->kind == cxx::Expr::Member) && o->semaSym && (o->semaSym->kind == Symbol::Function || o->semaSym->kind == Symbol::FunctionTemplate)) return value(o);
            LValue lv = lvalue(o);
            if (lv.kind == LValue::None) return constant(0, t, line);
            return addressOf(lv, line);
        }
        if (op == "*") { LValue lv; lv.kind = LValue::Mem; lv.addr = value(e->kids[0]); lv.type = t; return load(lv, t, line); }
        if (op == "++" || op == "--") {
            LValue lv = lvalue(e->kids[0]);
            if (lv.kind == LValue::None) return constant(0, t, line);
            const sema::Type* ot = lv.type;
            int cur = load(lv, ot, line);
            int nv = incremented(cur, ot, op == "++", line);
            store(lv, nv, ot, line);
            return lv.bitWidth > 0 ? load(lv, ot, line) : nv;     // a bit-field holds the value cut to its width
        }
        int v = value(e->kids[0]);
        const sema::Type* ot = m_semaTypes.decay(exprType(e->kids[0]));
        if (op == "!") { int b = toBool(v, ot, line); IrInst in = make(IrInst::Cmp, m_semaTypes.basic(sema::Type::Bool), line); in.sub = "eq"; in.args.push_back(IrOperand::temp(b, m_types.boolType())); in.args.push_back(IrOperand::imm(0, m_types.boolType())); return emit(in); }
        if (op == "+") return convert(v, ot, t, line);
        v = convert(v, ot, t, line);
        if (op == "-" && t->kind == sema::Type::Float) {
            // -f is a float: negated as a double (the back end's sign mask is a double's), exact both ways
            const sema::Type* dt = m_semaTypes.basic(sema::Type::Double);
            IrInst in = make(IrInst::Un, dt, line);
            in.sub = "fneg";
            in.args.push_back(IrOperand::temp(convert(v, t, dt, line), oldType(dt)));
            return convert(emit(in), dt, t, line);
        }
        IrInst in = make(IrInst::Un, t, line);
        in.sub = op == "-" ? (t->isFloating() ? "fneg" : "neg") : "not";
        in.args.push_back(IrOperand::temp(v, oldType(t)));
        return emit(in);
    }
    case cxx::Expr::PostIncDec: {
        if (e->semaSym && e->semaSym->kind == Symbol::Function) return rvalue(operatorCall(e), e, line);
        LValue lv = lvalue(e->kids[0]);
        if (lv.kind == LValue::None) return constant(0, t, line);
        const sema::Type* ot = lv.type;
        int cur = load(lv, ot, line);
        int nv = incremented(cur, ot, e->text == "++", line);
        store(lv, nv, ot, line);
        return cur;
    }
    case cxx::Expr::Binary: {
        const std::string& op = e->text;
        if (e->semaSym && e->semaSym->kind == Symbol::Function) return rvalue(operatorCall(e), e, line);
        if ((op == ".*" || op == "->*") && e->kids.size() == 2) { LValue lv = lvalue(e); return load(lv, exprType(e), line); }
        if (op == "&&" || op == "||") {
            int slot = newLocal("tmp.logic", m_semaTypes.basic(sema::Type::Bool), false);
            LValue lv; lv.kind = LValue::Local; lv.local = slot; lv.type = m_semaTypes.basic(sema::Type::Bool);
            int tb = m_out->newBlock("logic.true"), fb = m_out->newBlock("logic.false"), join = m_out->newBlock("logic.join");
            condition(e, tb, fb);
            m_block = tb; store(lv, constant(1, lv.type, line), lv.type, line); br(join, line);
            m_block = fb; store(lv, constant(0, lv.type, line), lv.type, line); br(join, line);
            m_block = join;
            return load(lv, lv.type, line);
        }
        int a = value(e->kids[0]);
        int b = value(e->kids[1]);
        return binary(op, a, exprType(e->kids[0]), b, exprType(e->kids[1]), t, line);
    }
    case cxx::Expr::Assign: {
        const std::string& op = e->text;
        if (e->semaSym && e->semaSym->kind == Symbol::Function) {
            Symbol* fn = e->semaSym;
            if (fn->isImplicit && fn->name == "operator=") {
                LValue lv = lvalue(e->kids[0]);
                int src = value(e->kids[1]);
                if (lv.kind == LValue::None) return src;
                int dst = addressOf(lv, line);
                copyAssign(lv.type, dst, convert(src, m_semaTypes.pointer(m_semaTypes.unref(exprType(e->kids[1]))), m_semaTypes.pointer(lv.type), line), line);
                return dst;
            }
            return rvalue(operatorCall(e), e, line);
        }
        LValue lv = lvalue(e->kids[0]);
        if (lv.kind == LValue::None) { value(e->kids[1]); m_notes.push_back("L" + numText(line) + ": assignment to a non-lvalue dropped"); return constant(0, t, line); }
        const sema::Type* lt = lv.type;
        int rv;
        if (op == "=") rv = convert(value(e->kids[1]), exprType(e->kids[1]), lt, line);
        else {
            int cur = load(lv, lt, line);
            int r = value(e->kids[1]);
            const std::string bop = op.substr(0, op.size() - 1);
            const sema::Type* lu = m_semaTypes.unqualified(lt);
            const sema::Type* ru = m_semaTypes.unqualified(m_semaTypes.decay(exprType(e->kids[1])));
            if (lu->isArithmetic() && ru->isArithmetic()) {
                // a op= b is a = a op b: computed in the operands' common type, then converted back
                const sema::Type* common = bop == "<<" || bop == ">>" ? m_semaTypes.promoted(lu) : m_semaTypes.arithmeticResult(lu, ru);
                rv = convert(binary(bop, cur, lu, r, ru, common, line), common, lu, line);
            } else rv = binary(bop, cur, lt, r, exprType(e->kids[1]), lt, line);
        }
        store(lv, rv, lt, line);
        return lv.bitWidth > 0 ? load(lv, lt, line) : rv;         // a bit-field holds the value cut to its width
    }
    case cxx::Expr::Cond: {
        const sema::Type* ct = m_semaTypes.unqualified(t);
        int r = conditional(e, isClassType(ct) || e->semaLvalue);
        if (e->semaLvalue && !isClassType(ct)) { LValue m; m.kind = LValue::Mem; m.addr = r; m.type = ct; return load(m, ct, line); }
        return r;
    }
    case cxx::Expr::Comma:
        if (e->semaSym && e->semaSym->kind == Symbol::Function) return rvalue(operatorCall(e), e, line);   // a user-defined operator,
        value(e->kids[0]); return value(e->kids[1]);
    case cxx::Expr::SizeofExpr: case cxx::Expr::SizeofType: return constant(e->semaConst ? e->semaValue : 8, t, line);
    case cxx::Expr::New: {
        const sema::Type* pt = exprType(e);
        const sema::Type* elem = pt->kind == sema::Type::Pointer ? pt->inner : pt;
        bool isArray = e->type && e->type->decl && !e->type->decl->parts.empty() && e->type->decl->parts.back().kind == cxx::DeclaratorPart::Array;
        IrInst n = make(IrInst::Call, pt, line);
        // the class's own operator new / new[] (its own or inherited, as lookup finds it), unless ::new
        Symbol* classNew = !e->globalScope && e->placement.empty() && isClassType(elem) && elem->decl
            ? classOperatorDelete(elem->decl, 0, isArray ? "operator new[]" : "operator new", 1) : 0;
        if (classNew) need(classNew);
        {
            // not generated as cl does yet: refused (S132) rather than calling the wrong allocator
            const sema::Type* inner = m_semaTypes.unqualified(elem);
            while (inner->kind == sema::Type::Array) inner = m_semaTypes.unqualified(inner->inner);
            // (sema turned a placement new with arguments other than one address into a call of the operator new
            // it resolved, the class's own included: that call is the address)
            const bool resolvedCall = e->placement.size() == 1 && e->placement[0]->kind == cxx::Expr::Call && e->placement[0]->semaSym && e->placement[0]->semaSym->name == "operator new";
            if (!resolvedCall && !e->globalScope && !e->placement.empty() && isClassType(elem) && elem->decl && classOperatorDelete(elem->decl, 0, isArray ? "operator new[]" : "operator new", 16))
                unsupported(line, "a placement new-expression of '" + elem->decl->qualifiedName() + "', a class with its own operator new, is not supported yet by --compile6");
            if (!e->globalScope && elem->kind == sema::Type::Array && isClassType(inner) && inner->decl
                && (classOperatorDelete(inner->decl, 0, "operator new[]", 16) || classOperatorDelete(inner->decl, 0, "operator delete[]", 16)))
                unsupported(line, "new of a multidimensional array of '" + inner->decl->qualifiedName() + "', a class with its own operator new[] or delete[], is not supported yet by --compile6");
        }
        if (isArray) {
            cxx::Expr* countExpr = e->type->decl->parts.back().arraySize;
            const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
            int count = countExpr ? convert(value(countExpr), exprType(countExpr), lng, line) : constant(1, lng, line);
            int bytes = binary("*", count, lng, constant(sizeOf(elem) > 0 ? sizeOf(elem) : 1, lng, line), lng, lng, line);
            bool header = needsDtor(elem);   // the element count is kept in front of the elements for delete[]
            if (header) bytes = binary("+", bytes, lng, constant(8, lng, line), lng, lng, line);   // an 8-byte cookie, as MSVC
            n.sub = classNew ? irName(classNew) : std::string("__new_array");
            n.args.push_back(IrOperand::temp(bytes, oldType(lng)));
            int p = emit(n);
            if (header) {
                LValue hl; hl.kind = LValue::Mem; hl.addr = p; hl.type = lng;
                store(hl, count, lng, line);
                IrInst adv = make(IrInst::AddrField, pt, line); adv.sub = "elements"; adv.extra = 8; adv.args.push_back(IrOperand::temp(p, oldType(pt)));
                p = emit(adv);
            }
            if (e->hasInit) {
                // new T[n](): every element value-initialised (valueInit): zeroed unless a user-declared constructor
                // builds it, then constructed below
                const sema::Type* ec = m_semaTypes.unqualified(elem);
                while (ec->kind == sema::Type::Array) ec = m_semaTypes.unqualified(ec->inner);
                if (!(ec->kind == sema::Type::Class && ec->decl && ec->decl->hasUserCtor)) {
                    IrInst z = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
                    z.sub = "__zero";
                    z.args.push_back(IrOperand::temp(p, oldType(m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void)))));
                    z.args.push_back(IrOperand::imm(0, m_types.intType()));
                    z.args.push_back(IrOperand::temp(binary("*", count, lng, constant(sizeOf(elem), lng, line), lng, lng, line), oldType(lng)));
                    emit(z);
                }
            }
            if (needsDefaultCtor(elem)) {
                // an element's constructor that throws: the elements built are destroyed (forEachElement), then
                // the memory is freed through the matching operator delete[] (C++98 5.3.4/17), as cl does
                Symbol* fc = !e->globalScope && isClassType(elem) && elem->decl ? elem->decl : 0;
                // without a cookie (no destructor) a sized operator delete[] is given count * size, as cl
                IrInst raw = make(IrInst::AddrIndex, pt, line); raw.extra = 1;
                raw.args.push_back(IrOperand::temp(p, oldType(pt))); raw.args.push_back(IrOperand::imm(header ? -8 : 0, oldType(lng)));
                const int k = registerFree(emit(raw), fc, true, count, sizeOf(elem), line);
                m_flagged[static_cast<size_t>(k)].noCookie = !header;
                forEachElement(elem, p, count, true, line);
                clearFlag(k, line);
            }
            return p;
        }
        int p;
        if (!e->placement.empty()) {
            // placement new: the object is built at the given address
            p = convert(value(e->placement[0]), m_semaTypes.decay(exprType(e->placement[0])), pt, line);
            for (size_t k = 1; k < e->placement.size(); ++k) value(e->placement[k]);
        } else {
            n.sub = classNew ? irName(classNew) : std::string("__new");
            n.args.push_back(IrOperand::imm(sizeOf(elem) > 0 ? sizeOf(elem) : 1, oldType(m_semaTypes.sizeType())));
            p = emit(n);
        }
        // a constructor (or initializer) that throws: the memory is freed through the matching operator delete
        // (C++98 5.3.4/17), as cl does; a placement new's matching operator delete frees nothing
        int freeK = -1;
        if (e->placement.empty() && ((e->semaSym && e->semaSym->isConstructor) || !e->kids.empty() || (isClassType(elem) && needsDefaultCtor(elem))))
            freeK = registerFree(p, !e->globalScope && isClassType(elem) && elem->decl ? elem->decl : 0, false, -1, sizeOf(elem), line);
        if (e->hasInit && e->kids.empty() && isClassType(elem)) valueInit(e->semaSym && e->semaSym->isConstructor ? e->semaSym : 0, elem, p, true, line);   // new T(); new T stays uninitialised
        else if (e->semaSym && e->semaSym->isConstructor) construct(e->semaSym, p, e->kids, 0, line);
        else if (!e->kids.empty()) { LValue lv; lv.kind = LValue::Mem; lv.addr = p; lv.type = elem; store(lv, convert(value(e->kids[0]), exprType(e->kids[0]), elem, line), elem, line); }
        else if (e->hasInit && !isClassType(elem)) { LValue lv; lv.kind = LValue::Mem; lv.addr = p; lv.type = elem; store(lv, constant(0, elem, line), elem, line); }   // new T(): value-initialised
        else if (isClassType(elem)) constructDefault(elem, p, line);
        if (freeK >= 0) clearFlag(freeK, line);
        return p;
    }
    case cxx::Expr::Delete: {
        int p = value(e->kids[0]);
        const sema::Type* pt = m_semaTypes.decay(exprType(e->kids[0]));
        if (e->arrayDelete && !e->globalScope && pt->kind == sema::Type::Pointer && m_semaTypes.unqualified(pt->inner)->kind == sema::Type::Array) {
            const sema::Type* inner = m_semaTypes.unqualified(pt->inner);
            while (inner->kind == sema::Type::Array) inner = m_semaTypes.unqualified(inner->inner);
            if (isClassType(inner) && inner->decl && (classOperatorDelete(inner->decl, 0, "operator new[]", 16) || classOperatorDelete(inner->decl, 0, "operator delete[]", 16)))
                unsupported(line, "delete[] of a multidimensional array of '" + inner->decl->qualifiedName() + "', a class with its own operator new[] or delete[], is not supported yet by --compile6");
        }
        bool dtors = pt->kind == sema::Type::Pointer && (needsDtor(pt->inner) || (!e->arrayDelete && isClassType(pt->inner) && pt->inner->decl && pt->inner->decl->isPolymorphic));
        int xb = -1;
        if (dtors) {
            // a null pointer is skipped
            int db = m_out->newBlock("delete.do");
            xb = m_out->newBlock("delete.end");
            condbr(toBool(p, pt, line), db, xb, line);
            m_block = db;
        }
        Symbol* vdtor = !e->arrayDelete && pt->kind == sema::Type::Pointer && isClassType(pt->inner) && pt->inner->decl ? findSpecial(pt->inner->decl, '~') : 0;
        if (vdtor && vdtor->isVirtual && vdtor->vtableSlot >= 0) {
            // a virtual destructor: the slot's deleting destructor destroys the complete object and frees it (flags
            // 1), as cl calls it, so a pointer to a secondary base frees the right address
            need(vdtor);
            const sema::Type* cp = m_semaTypes.pointer(m_semaTypes.classType(vdtor->ownerClass));
            IrInst in = make(IrInst::VCall, m_semaTypes.basic(sema::Type::Void), line);
            in.sub = irName(vdtor);
            in.extra = vdtor->vtableSlot;
            in.args.push_back(IrOperand::temp(vfptrThis(vdtor, p, line), oldType(cp)));
            in.args.push_back(IrOperand::imm(1, oldType(m_semaTypes.basic(sema::Type::UInt))));
            emit(in);
            br(xb, line);
            m_block = xb;
            return constant(0, m_semaTypes.basic(sema::Type::Int), line);
        }
        Symbol* adtor = dtors && e->arrayDelete && isClassType(pt->inner) && pt->inner->decl ? findSpecial(pt->inner->decl, '~') : 0;
        if (adtor && adtor->isVirtual && adtor->vtableSlot >= 0) {
            // delete[] of a class with a virtual destructor, as cl: an empty array is freed from its cookie, any
            // other goes to the slot's vector deleting destructor with flags 3 (destroy the elements, free)
            need(adtor);
            const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
            const sema::Type* cp = m_semaTypes.pointer(m_semaTypes.classType(adtor->ownerClass));
            IrInst hd = make(IrInst::AddrIndex, pt, line); hd.extra = 1; hd.args.push_back(IrOperand::temp(p, oldType(pt))); hd.args.push_back(IrOperand::imm(-8, oldType(lng)));
            const int cookie = emit(hd);
            LValue hl; hl.kind = LValue::Mem; hl.addr = cookie; hl.type = lng;
            const int count = load(hl, lng, line);
            int vb = m_out->newBlock("delete.vector"), zb = m_out->newBlock("delete.empty");
            condbr(toBool(count, lng, line), vb, zb, line);
            m_block = zb;
            freeArray(pt->inner->decl, cookie, count, sizeOf(pt->inner), line);
            br(xb, line);
            m_block = vb;
            IrInst in = make(IrInst::VCall, m_semaTypes.basic(sema::Type::Void), line);
            in.sub = irName(adtor);
            in.extra = adtor->vtableSlot;
            in.args.push_back(IrOperand::temp(vfptrThis(adtor, p, line), oldType(cp)));
            in.args.push_back(IrOperand::imm(3, oldType(m_semaTypes.basic(sema::Type::UInt))));
            emit(in);
            br(xb, line);
            m_block = xb;
            return constant(0, m_semaTypes.basic(sema::Type::Int), line);
        }
        if (!e->arrayDelete && pt->kind == sema::Type::Pointer) destroy(pt->inner, p, line, true);
        else if (e->arrayDelete && pt->kind == sema::Type::Pointer && needsDtor(pt->inner)) {
            const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
            IrInst hd = make(IrInst::AddrIndex, pt, line); hd.extra = 1; hd.args.push_back(IrOperand::temp(p, oldType(pt))); hd.args.push_back(IrOperand::imm(-8, oldType(lng)));
            int header = emit(hd);
            LValue hl; hl.kind = LValue::Mem; hl.addr = header; hl.type = lng;
            const int count = load(hl, lng, line);
            forEachElement(pt->inner, p, count, false, line);
            // from the cookie through the class's operator delete[], or the global one, with count * size + 8, as cl
            freeArray(isClassType(pt->inner) && !e->globalScope ? pt->inner->decl : 0, header, count, sizeOf(pt->inner), line);
        } else if (e->arrayDelete) {
            // no cookie: the class's operator delete[] (a two-parameter one gets one element's size, as cl), or the
            // global one
            const sema::Type* inner = pt->kind == sema::Type::Pointer ? m_semaTypes.unqualified(pt->inner) : 0;
            freeArray(inner && isClassType(inner) && !e->globalScope ? inner->decl : 0, p, -1, inner ? sizeOf(inner) : 1, line, false);
        }
        if (!e->arrayDelete && (pt->kind != sema::Type::Pointer || !isClassType(pt->inner) || e->globalScope || !freeObject(pt->inner->decl, p, line)))
            freeGlobal(p, pt->kind == sema::Type::Pointer ? sizeOf(pt->inner) : 1, line);
        if (xb >= 0) { br(xb, line); m_block = xb; }
        return constant(0, m_semaTypes.basic(sema::Type::Int), line);
    }
    case cxx::Expr::Throw: {
        IrInst in = make(IrInst::Throw, m_semaTypes.basic(sema::Type::Void), line);
        if (!e->kids.empty()) {
            const sema::Type* tt = m_semaTypes.unqualified(m_semaTypes.decay(exprType(e->kids[0])));
            if (isClassType(tt) && tt->decl) {
                // the exception object: copy-constructed into heap storage the runtime takes over and
                // destroys with the class's destructor when the handler ends
                IrInst n = make(IrInst::Call, m_semaTypes.pointer(tt), line);
                n.sub = "__malloc";   // the runtime frees it
                n.args.push_back(IrOperand::imm(sizeOf(tt) > 0 ? sizeOf(tt) : 1, m_types.intType()));
                int obj = emit(n);
                materialize(e->kids[0], tt, obj, line);
                endFullExpr(line);
                in.args.push_back(IrOperand::temp(obj, oldType(tt)));
                Symbol* dtor = needsDtor(tt) ? (hasVbases(tt->decl) ? vbaseDestructor(tt->decl) : findSpecial(tt->decl, '~')) : 0;   // a complete object: ??_D with virtual bases
                if (dtor) { need(dtor); IrInst fa = make(IrInst::AddrGlobal, 0, line); fa.type = m_types.function(oldType(m_semaTypes.basic(sema::Type::Void))); fa.args.push_back(IrOperand::global(irName(dtor), fa.type)); in.args.push_back(IrOperand::temp(emit(fa), fa.type)); }
                else in.args.push_back(IrOperand::imm(0, oldType(m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void)))));
            } else {
                int v = value(e->kids[0]);
                in.args.push_back(IrOperand::temp(v, oldType(tt)));
            }
            const sema::Type* desc = tt;
            cxx::Expr* operand = e->kids[0];
            while (operand && operand->kind == cxx::Expr::Paren && !operand->kids.empty()) operand = operand->kids[0];
            // cl throws a string literal as a pointer to non-const characters: catch (char*) takes it
            if (operand && operand->kind == cxx::Expr::StringLit && tt->kind == sema::Type::Pointer) desc = m_semaTypes.pointer(m_semaTypes.unqualified(tt->inner));
            in.sub = throwDescriptorFor(desc);
            in.extra = isClassType(tt) ? sizeOf(tt) : 8;
        }
        emit(in);
        return constant(0, m_semaTypes.basic(sema::Type::Int), line);
    }
    case cxx::Expr::Typeid: return typeidAddress(e, line);
    case cxx::Expr::InitList: case cxx::Expr::Error: return constant(0, t, line);
    }
    return constant(0, t, line);
}

// ---- statements -----------------------------------------------------------------------------------------

// true when the elements initialise every scalar of t, so that nothing is left to zero
bool IrGen::coversAll(const sema::Type* t, const std::vector<cxx::Expr*>& elems) const
{
    const sema::Type* u = m_semaTypes.unref(t);
    size_t want = 0;
    if (u->kind == sema::Type::Array) want = u->arraySize > 0 ? static_cast<size_t>(u->arraySize) : 0;
    else if (u->kind == sema::Type::Class && u->decl) want = u->decl->isUnion ? 1 : u->decl->fields.size();
    if (elems.size() < want) return false;
    for (size_t i = 0; i < elems.size(); ++i) {
        if (elems[i]->kind == cxx::Expr::Error) return false;
        if (elems[i]->kind != cxx::Expr::InitList) continue;
        const sema::Type* et = u->kind == sema::Type::Array ? u->inner : (i < u->decl->fields.size() ? u->decl->fields[i]->type : 0);
        if (!et || !coversAll(et, elems[i]->kids)) return false;
    }
    return true;
}

// brace initialisation of an aggregate whose braces sema made explicit: an InitList element is a nested
// aggregate; members without an initializer are value-initialised (static storage is zero already)
void IrGen::initAggregate(const sema::Type* t, int addr, const std::vector<cxx::Expr*>& elems, bool zeroRest, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (zeroRest && !coversAll(u, elems)) {
        const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
        IrInst z = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
        z.sub = "__zero";
        z.args.push_back(IrOperand::temp(addr, oldType(vp)));
        z.args.push_back(IrOperand::imm(0, m_types.intType()));
        z.args.push_back(IrOperand::imm(sizeOf(u), m_types.builtin(Type::LongLong)));
        emit(z);
    }
    size_t parts = 0;
    if (u->kind == sema::Type::Array) parts = u->arraySize > 0 ? static_cast<size_t>(u->arraySize) : 0;
    else if (u->kind == sema::Type::Class && u->decl) parts = u->decl->isUnion ? (u->decl->fields.empty() ? 0 : 1) : u->decl->fields.size();
    for (size_t i = 0; i < parts; ++i) {
        const sema::Type* et;
        int eaddr;
        if (u->kind == sema::Type::Array) {
            et = u->inner;
            IrInst ai = make(IrInst::AddrIndex, m_semaTypes.pointer(et), line); ai.extra = sizeOf(et); ai.args.push_back(IrOperand::temp(addr, oldType(m_semaTypes.pointer(et)))); ai.args.push_back(IrOperand::imm(static_cast<long>(i), m_types.builtin(Type::LongLong)));
            eaddr = emit(ai);
        } else {
            Symbol* fld = u->decl->fields[i];
            et = fld->type;
            eaddr = subobject(addr, u, fld->name, fld->offset, et, line);
        }
        if (i >= elems.size()) { constructDefault(et, eaddr, line); nullMemberPointers(et, eaddr, line); continue; }
        cxx::Expr* x = elems[i];
        if (x->kind == cxx::Expr::Error) continue;
        if (x->kind == cxx::Expr::InitList) { initAggregate(et, eaddr, x->kids, false, line); continue; }
        if (isClassType(et)) { materialize(x, et, eaddr, line); continue; }
        LValue el; el.kind = LValue::Mem; el.addr = eaddr; el.type = et;
        if (u->kind == sema::Type::Class) bitField(el, u->decl->fields[i]);
        store(el, convert(value(x), exprType(x), et, line), et, line);
    }
}

void IrGen::initObject(const LValue& lv, const sema::Type* t, Symbol* var, cxx::InitDeclarator& id, int line)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (isReference(t)) {
        if (id.init.args.empty()) return;
        cxx::Expr* initE = id.init.args[0];
        size_t tempsBefore = m_temps.size();
        // a temporary of the referenced type when the initializer is a prvalue or of another type (8.5.3/5):
        // `const int& r = 5`, `const double& d = i`, `const std::string& s = "x"` (a converting constructor); it
        // lives as long as the reference (its cleanup is the block's); a class of the referenced type or derived
        // from it, and every lvalue of the type, is bound directly
        const sema::Type* srcT = m_semaTypes.unqualified(m_semaTypes.unref(exprType(initE)));
        LValue src;
        const bool classTarget = isClassType(u);
        bool viaTemp = classTarget ? (srcT->kind != sema::Type::Class || (srcT != u && !(srcT->decl && u->decl && isBaseOf(srcT->decl, u->decl))))
                                   : (srcT != u && !(srcT->kind == sema::Type::Array && u->kind == sema::Type::Array) && !(srcT->kind == sema::Type::Function && u->kind == sema::Type::Function));
        if (!viaTemp) { src = lvalue(initE); if (src.kind == LValue::None && !classTarget) viaTemp = true; }
        int addr;
        if (viaTemp && lv.kind == LValue::Local) {
            int slot = newLocal("tmp.ref", u, true);
            LValue tl; tl.kind = LValue::Local; tl.local = slot; tl.type = u;
            if (classTarget) { materialize(initE, u, addressOf(tl, line), line); if (needsDtor(u)) registerCleanup(slot, u, false, 1, line); }
            else store(tl, convert(value(initE), exprType(initE), u, line), u, line);
            addr = addressOf(tl, line);
        } else if (viaTemp) {
            // a namespace-scope or static reference: its temporary is static storage of this module, destroyed
            // with the globals when it has a destructor
            IrGlobal g;
            g.name = lv.global + "$ref";
            g.size = sizeOf(u) > 0 ? sizeOf(u) : 8;
            g.isFloat = u->isFloating();
            g.internal = true;
            g.align = alignOf(u);
            if (m_x86) m_x86->addGlobal(g);
            LValue tl; tl.kind = LValue::Global; tl.global = g.name; tl.type = u;
            if (classTarget) { materialize(initE, u, addressOf(tl, line), line); if (needsDtor(u)) registerAtExit(g.name, u, line); }
            else store(tl, convert(value(initE), exprType(initE), u, line), u, line);
            addr = addressOf(tl, line);
        } else {
            addr = src.kind == LValue::None ? value(initE) : addressOf(src, line);
            addr = convert(addr, m_semaTypes.pointer(src.kind == LValue::None ? m_semaTypes.unref(exprType(initE)) : src.type), m_semaTypes.pointer(u), line);
        }
        if (lv.kind == LValue::Local) { IrInst st = make(IrInst::StoreLocal, t, line); st.args.push_back(IrOperand::local(lv.local, oldType(t))); st.args.push_back(IrOperand::temp(addr, oldType(m_semaTypes.pointer(u)))); emit(st); }
        else { IrInst st = make(IrInst::StoreGlobal, t, line); st.args.push_back(IrOperand::global(lv.global, oldType(t))); st.args.push_back(IrOperand::temp(addr, oldType(m_semaTypes.pointer(u)))); emit(st); }
        // 12.2/5: the temporary the reference is bound to (or whose subobject it is bound to) lives as long as
        // the reference; the other temporaries of the initializer (the arguments of its constructor, a temporary
        // passed through a function returning a reference: `const T& r = pass(T())`) die at the end of the
        // full expression. The bound one is the last registered when the initializer is a class prvalue (its
        // own temporary is registered after its arguments'); a conditional keeps both arms' (guarded by their flags)
        if (viaTemp) keepTemporaries(tempsBefore);
        else {
            cxx::Expr* x = initE;
            for (;;) {
                if (x->kind == cxx::Expr::Paren && !x->kids.empty()) x = x->kids[0];
                else if (x->kind == cxx::Expr::Member && !x->arrowMember && !x->kids.empty() && isClassType(exprType(x->kids[0]))) x = x->kids[0];
                else if (x->kind == cxx::Expr::Index && x->kids.size() == 2 && m_semaTypes.unref(exprType(x->kids[0]))->kind == sema::Type::Array) x = x->kids[0];
                else break;
            }
            if (x->kind == cxx::Expr::Cond) keepTemporaries(tempsBefore);
            else if (isClassType(exprType(x)) && !x->semaLvalue && m_temps.size() > tempsBefore) { m_cleanups.push_back(m_temps.back()); m_temps.pop_back(); }
        }
        return;
    }
    if (u->kind == sema::Type::Class && u->decl) {
        int addr = addressOf(lv, line);
        cxx::Expr* src = id.init.args.size() == 1 ? id.init.args[0] : 0;
        bool sameClass = src && isClassType(exprType(src)) && m_semaTypes.unqualified(m_semaTypes.unref(exprType(src))) == u;
        if (id.init.kind == cxx::Initializer::Assign && src) {
            if (sameClass) { materialize(src, u, addr, line); return; }
            if (src->semaSym && src->semaSym->kind == Symbol::Function && src->semaSym->isConstructor && src->semaSym->ownerClass == u->decl) { construct(src->semaSym, addr, id.init.args, 0, line); return; }   // not a derived class's constructor (V v = D())
            if (var->initCtor) { construct(var->initCtor, addr, id.init.args, 0, line); return; }
            materialize(src, u, addr, line);
            return;
        }
        if (id.init.kind == cxx::Initializer::Parens && sameClass && (!var->initCtor || var->initCtor == findSpecial(u->decl, 'c'))) { materialize(src, u, addr, line); return; }
        if (var->initCtor) { construct(var->initCtor, addr, id.init.args, 0, line); return; }
        if (id.init.kind == cxx::Initializer::None) { constructDefault(u, addr, line); return; }
        if (id.init.kind == cxx::Initializer::Brace) initAggregate(u, addr, id.init.args, lv.kind == LValue::Local, line);
        return;
    }
    if (u->kind == sema::Type::Array) {
        int addr = addressOf(lv, line);
        const sema::Type* lt = id.init.args.size() == 1 && id.init.args[0]->kind == cxx::Expr::StringLit ? m_semaTypes.unref(exprType(id.init.args[0])) : 0;
        const int elem = sizeOf(u->inner);
        if (id.init.kind != cxx::Initializer::Brace && lt && lt->kind == sema::Type::Array && lt->inner && sizeOf(lt->inner) == elem && (elem == 1 || elem == 2)) {
            // char s[N] = "...", wchar_t w[N] = L"...": the literal's units with its terminator; the rest of a
            // longer local array is zero (static storage is zero already)
            const long units = u->arraySize < lt->arraySize ? u->arraySize : lt->arraySize;
            const long bytes = units * elem;
            const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
            if (lv.kind == LValue::Local && u->arraySize > units) {
                IrInst z = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
                z.sub = "__zero";
                z.args.push_back(IrOperand::temp(addr, oldType(vp)));
                z.args.push_back(IrOperand::imm(0, m_types.intType()));
                z.args.push_back(IrOperand::imm(u->arraySize * elem, m_types.builtin(Type::LongLong)));
                emit(z);
            }
            const int src = value(id.init.args[0]);
            IrInst c = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
            c.sub = "__copy";
            c.args.push_back(IrOperand::temp(addr, oldType(vp)));
            c.args.push_back(IrOperand::temp(src, oldType(vp)));
            c.args.push_back(IrOperand::imm(bytes, m_types.builtin(Type::LongLong)));
            emit(c);
            return;
        }
        if (id.init.kind != cxx::Initializer::Brace) { constructDefault(u, addr, line); return; }
        initAggregate(u, addr, id.init.args, lv.kind == LValue::Local, line);
        return;
    }
    if (id.init.args.empty()) return;
    cxx::Expr* src = id.init.args[0];
    int v = convert(value(src), exprType(src), u, line);   // a conversion operator was wrapped around src by sema
    store(lv, v, u, line);
}

void IrGen::localDecl(cxx::Decl* d)
{
    if (d->kind != cxx::Decl::Simple) return;
    for (size_t i = 0; i < d->declarators.size(); ++i) {
        cxx::InitDeclarator& id = d->declarators[i];
        Symbol* var = id.decl ? id.decl->semaSym : 0;
        if (!var || var->kind != Symbol::Variable) continue;
        if (!var->isLocal) continue;   // a block-scope extern declaration: the namespace's variable, built there
        if (var->isStatic) {
            // a static local is a global of this module; an initializer that is not a constant runs on the first pass
            const int line = id.decl->loc.line;
            // named after the function and numbered within it, so every unit that compiles an inline function
            // emits the same symbol (the global is then a COMDAT like the function)
            std::string gname = m_out->name + "::" + var->name + "#" + numText(m_staticCount++);
            m_staticNames[var] = gname;
            long block = 2;
            if (m_msvcNames && m_x86) {
                // cl numbers the blocks of a function; the body is 2 (numberStaticBlocks). Without that number
                // (no body walked): 2 plus the block's position among the function's blocks, unique only
                std::map<Symbol*, long>::const_iterator nb = m_staticBlocks.find(var);
                if (nb != m_staticBlocks.end()) block = nb->second;
                else if (var->scope && !(var->scope->parent && var->scope->parent->kind == Scope::Function)) {
                    long k = 0;
                    for (size_t si = 0; si < m_prog.scopes.size(); ++si) {
                        const Scope* sc = m_prog.scopes[si];
                        if (sc->kind != Scope::Block || sc->owner != var->scope->owner) continue;
                        if (sc == var->scope) { block = 2 + k; break; }
                        ++k;
                    }
                }
                m_x86->registerGlobalName(gname, msvcMangleStaticLocal(*var, *m_fn, block));
            }
            const sema::Type* vt = m_semaTypes.unqualified(m_semaTypes.unref(var->type));
            IrGlobal g;
            g.name = gname;
            g.size = isReference(var->type) ? 8 : (sizeOf(vt) > 0 ? sizeOf(vt) : 8);   // a reference is a pointer slot
            g.isFloat = !isReference(var->type) && vt->isFloating();
            g.linkOnce = m_out->linkOnce;
            // internal unless the function is inline: then every unit's copy is one COMDAT the linker picks, as cl's
            g.internal = !g.linkOnce;
            g.align = isReference(var->type) ? 8 : alignOf(vt);
            if (var->declAlign > g.align) g.align = var->declAlign;
            bool scalarConstant = id.init.kind != cxx::Initializer::None && id.init.args.size() == 1 && id.init.args[0]->semaConst && vt->isScalar() && vt->kind != sema::Type::MemberPointer && !isReference(var->type) && !vt->isFloating();
            if (scalarConstant) g.init = numText(id.init.args[0]->semaValue);
            // a table of constants and addresses is static data, built before anything runs (no guard)
            else if (id.init.kind != cxx::Initializer::None && !isReference(var->type) && !needsDtor(vt) && !var->initCtor) {
                std::vector<IrDataItem> items;
                if (staticData(var->type, id.init, items)) { g.data = items; g.readOnly = readOnlyObject(var->type); scalarConstant = true; }
            }
            else if (id.init.kind == cxx::Initializer::None && !isReference(var->type)) {
                // zero-initialised, holding pointers to members: their null values, as cl writes them
                std::vector<IrDataItem> items;
                if (nullMemberPointerData(vt, items, true)) g.data = items;
            }
            if (m_x86) m_x86->addGlobal(g);
            const bool destroyed = !isReference(var->type) && needsDtor(vt);   // registered with atexit once built
            bool needsCode = !scalarConstant && (id.init.kind != cxx::Initializer::None || needsDefaultCtor(vt) || destroyed);
            if (needsCode) {
                // cl's thread-safe statics, so that an inline function's object (a COMDAT shared with cl's units)
                // is built once whichever side runs first: an int guard "$TSS<n>", 0 before, -1 while some
                // thread builds the object, then the CRT's epoch (negative). A guard of 0 or -1 leads to
                // _Init_thread_header, which leaves it -1 when this thread is to run the initializer (it then
                // calls _Init_thread_footer) and waits for another thread that is running it. cl's fast path
                // compares with the thread's epoch instead (a TLS read); this test is equivalent for its values
                const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
                const sema::Type* ut = m_semaTypes.basic(sema::Type::UInt);
                const sema::Type* b = m_semaTypes.basic(sema::Type::Bool);
                IrGlobal guard;
                guard.name = gname + "$guard";
                guard.size = 4;
                guard.align = 4;
                guard.isFloat = false;
                guard.linkOnce = m_out->linkOnce;
                guard.internal = !guard.linkOnce;
                if (m_x86) {
                    if (m_msvcNames) m_x86->registerGlobalName(guard.name, msvcMangleStaticGuard(*m_fn, block, m_guardCount));
                    m_x86->addGlobal(guard);
                }
                ++m_guardCount;
                LValue gl; gl.kind = LValue::Global; gl.global = guard.name; gl.type = it;
                int done = m_out->newBlock("static.done"), check = m_out->newBlock("static.lock"), doInit = m_out->newBlock("static.init");
                // one statement per emitted value: the order of a call's arguments differs between compilers,
                // and the self-built compiler must produce the same code as the MSVC-built one
                int g0 = load(gl, it, line);
                int one = constant(1, ut, line);
                int biased = binary("+", g0, ut, one, ut, ut, line);
                int one2 = constant(1, ut, line);
                condbr(binary("<=", biased, ut, one2, ut, b, line), check, done, line);
                m_block = check;
                IrInst enter = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
                enter.sub = "_Init_thread_header";
                enter.args.push_back(IrOperand::temp(addressOf(gl, line), oldType(m_semaTypes.pointer(it))));
                emit(enter);
                int g1 = load(gl, it, line);
                int minus1 = constant(-1, it, line);
                condbr(binary("==", g1, it, minus1, it, b, line), doInit, done, line);
                m_block = doInit;
                // an initializer that throws hands the guard to _Init_thread_abort (as cl's unwind funclet
                // does): an unwind-only cleanup whose flag is set while the initializer runs
                Cleanup ac;
                ac.type = it;
                ac.unwindOnly = true;
                ac.abortGuard = guard.name;
                ac.flag = newLocal("flag." + guard.name, b, true);
                LValue afl; afl.kind = LValue::Local; afl.local = ac.flag; afl.type = b;
                int on = constant(1, b, line);
                store(afl, on, b, line);
                m_flagged.push_back(ac);
                LValue lv; lv.kind = LValue::Global; lv.global = gname; lv.type = m_semaTypes.unref(var->type);
                initObject(lv, var->type, var, id, line);
                endFullExpr(line);
                int off = constant(0, b, line);
                store(afl, off, b, line);
                if (destroyed) registerAtExit(gname, lv.type, line);
                IrInst leave = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
                leave.sub = "_Init_thread_footer";
                leave.args.push_back(IrOperand::temp(addressOf(gl, line), oldType(m_semaTypes.pointer(it))));
                emit(leave);
                br(done, line);
                m_block = done;
            }
            continue;
        }
        if (var == m_nrvo) {
            // NRVO: built in the caller's result object; the returns copy nothing, and only an exception that
            // leaves the function destroys it
            const int line = id.decl->loc.line;
            const sema::Type* vt = var->type;
            LValue sl; sl.kind = LValue::Local; sl.local = m_sretSlot; sl.type = m_semaTypes.pointer(vt);
            LValue rl; rl.kind = LValue::Mem; rl.addr = load(sl, sl.type, line); rl.type = vt;
            // the name is in scope in its own initializer (A a = &a): reads go through the hidden pointer
            m_slots[var] = m_sretSlot;
            var->type = m_semaTypes.reference(vt);
            initObject(rl, vt, var, id, line);
            if (needsDtor(vt)) registerCleanup(m_sretSlot, vt, true, 2, line);
            endFullExpr(line);
            continue;
        }
        int slot = newLocal(var->name, var->type, false);
        m_slots[var] = slot;
        LValue lv; lv.kind = LValue::Local; lv.local = slot; lv.type = m_semaTypes.unref(var->type);
        initObject(lv, var->type, var, id, id.decl->loc.line);
        if (!isReference(var->type) && needsDtor(var->type)) registerCleanup(slot, var->type, false, 0, id.decl->loc.line);
        endFullExpr(id.decl->loc.line);
    }
}

int IrGen::labelBlock(const std::string& name)
{
    std::map<std::string, int>::iterator it = m_labels.find(name);
    if (it != m_labels.end()) return it->second;
    int b = m_out->newBlock("label." + name);
    m_labels[name] = b;
    return b;
}

void IrGen::scopedStmt(cxx::Stmt* s, int line)
{
    openScope();
    stmt(s);
    closeScope(line);
}

// a declaration as the condition of if, while, for or switch: the variable is built (in the scope the caller
// opened) and its value returned, a class object's through its conversion function; t receives the value's type
int IrGen::conditionDecl(cxx::Decl* d, const sema::Type*& t, int line)
{
    localDecl(d);
    Symbol* v = d->declarators.empty() || !d->declarators[0].decl ? 0 : d->declarators[0].decl->semaSym;
    t = m_semaTypes.basic(sema::Type::Int);
    if (!v || m_slots.find(v) == m_slots.end()) return constant(0, t, line);
    LValue lv; lv.kind = LValue::Local; lv.local = m_slots[v]; lv.type = m_semaTypes.unref(v->type);
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(v->type));
    if (u && u->kind == sema::Type::Class && u->decl && u->decl->members) {
        Symbol* conv = 0;
        const std::vector<Symbol*>& ms = u->decl->members->order;
        for (size_t i = 0; i < ms.size() && !conv; ++i)
            if (ms[i]->kind == Symbol::Function && ms[i]->isConversion && ms[i]->type && ms[i]->type->inner && ms[i]->type->inner->isScalar()) conv = ms[i];
        if (!conv) { m_notes.push_back("L" + numText(line) + ": no conversion of the condition's " + u->decl->qualifiedName()); return constant(0, t, line); }
        std::vector<cxx::Expr*> none;
        int obj = isReference(v->type) ? load(lv, v->type, line) : addressOf(lv, line);
        obj = adjustThis(conv, obj, u, line);
        t = conv->type->inner;
        return callFunction(conv, obj, -1, none, 0, true, line);
    }
    t = lv.type;
    return load(lv, v->type, line);
}

// a loop whose condition declares a variable: the variable is built from the condition on every pass and
// destroyed at the end of each pass, when the condition is false, and on break and continue (as cl does). The
// body and a break reach the latch, which destroys it and repeats (to next) while the condition held
void IrGen::declLoop(cxx::Stmt* s, int cb, int next, int xb, int line)
{
    const sema::Type* b = m_semaTypes.basic(sema::Type::Bool);
    const int bb = m_out->newBlock("loop.body"), latch = m_out->newBlock("loop.latch"), brk = m_out->newBlock("loop.break");
    const int ok = newLocal("cond.ok", b, false);
    LValue okl; okl.kind = LValue::Local; okl.local = ok; okl.type = b;
    m_block = cb;
    openScope();
    const sema::Type* t = 0;
    int c = toBool(conditionDecl(s->decl, t, line), t, line);
    endFullExpr(line);
    store(okl, c, b, line);
    condbr(c, bb, latch, line);
    LoopTargets lt; lt.breakBlock = brk; lt.continueBlock = latch; lt.tryDepth = m_tryStack.size(); lt.cleanupDepth = m_cleanups.size(); m_loops.push_back(lt);
    m_block = bb; scopedStmt(s->kids.empty() ? 0 : s->kids[0], line); br(latch, line);
    m_loops.pop_back();
    m_block = brk; store(okl, constant(0, b, line), b, line); br(latch, line);
    m_block = latch;
    closeScope(line);
    int again = load(okl, b, line);
    condbr(again, next, xb, line);
    m_block = xb;
}

void IrGen::stmt(cxx::Stmt* s)
{
    if (!s) return;
    const int line = s->loc.line;
    switch (s->kind) {
    case cxx::Stmt::Compound: openScope(); for (size_t i = 0; i < s->kids.size(); ++i) stmt(s->kids[i]); closeScope(line); return;
    case cxx::Stmt::Empty: case cxx::Stmt::Asm: case cxx::Stmt::Error: return;
    case cxx::Stmt::ExprStmt: value(s->expr); endFullExpr(line); return;
    case cxx::Stmt::DeclStmt: localDecl(s->decl); return;
    case cxx::Stmt::If: {
        int tb = m_out->newBlock("if.then"), eb = s->kids.size() > 1 ? m_out->newBlock("if.else") : -1, join = m_out->newBlock("if.end");
        openScope();   // a condition variable lives through both branches
        if (s->decl) { const sema::Type* t = 0; int c = toBool(conditionDecl(s->decl, t, line), t, line); endFullExpr(line); condbr(c, tb, eb >= 0 ? eb : join, line); }
        else condition(s->expr, tb, eb >= 0 ? eb : join);
        m_block = tb; scopedStmt(s->kids[0], line); br(join, line);
        if (eb >= 0) { m_block = eb; scopedStmt(s->kids[1], line); br(join, line); }
        m_block = join;
        closeScope(line);
        return;
    }
    case cxx::Stmt::While: {
        if (s->decl) {
            int dc = m_out->newBlock("while.cond"), dx = m_out->newBlock("while.end");
            br(dc, line);
            declLoop(s, dc, dc, dx, line);
            return;
        }
        int cb = m_out->newBlock("while.cond"), bb = m_out->newBlock("while.body"), xb = m_out->newBlock("while.end");
        br(cb, line);
        m_block = cb; condition(s->expr, bb, xb);
        LoopTargets lt; lt.breakBlock = xb; lt.continueBlock = cb; lt.tryDepth = m_tryStack.size(); lt.cleanupDepth = m_cleanups.size(); m_loops.push_back(lt);
        m_block = bb; scopedStmt(s->kids[0], line); br(cb, line);
        m_loops.pop_back();
        m_block = xb;
        return;
    }
    case cxx::Stmt::DoWhile: {
        int bb = m_out->newBlock("do.body"), cb = m_out->newBlock("do.cond"), xb = m_out->newBlock("do.end");
        br(bb, line);
        LoopTargets lt; lt.breakBlock = xb; lt.continueBlock = cb; lt.tryDepth = m_tryStack.size(); lt.cleanupDepth = m_cleanups.size(); m_loops.push_back(lt);
        m_block = bb; scopedStmt(s->kids[0], line); br(cb, line);
        m_loops.pop_back();
        m_block = cb; condition(s->expr, bb, xb);
        m_block = xb;
        return;
    }
    case cxx::Stmt::For: {
        openScope();   // the init-statement's variables live until the loop ends
        if (s->init) stmt(s->init);
        if (s->decl) {
            int cb = m_out->newBlock("for.cond"), sb = m_out->newBlock("for.step"), xb = m_out->newBlock("for.end");
            br(cb, line);
            declLoop(s, cb, sb, xb, line);
            m_block = sb; if (s->expr2) { value(s->expr2); endFullExpr(line); } br(cb, line);
            m_block = xb;
            closeScope(line);
            return;
        }
        int cb = m_out->newBlock("for.cond"), bb = m_out->newBlock("for.body"), sb = m_out->newBlock("for.step"), xb = m_out->newBlock("for.end");
        br(cb, line);
        m_block = cb;
        if (s->expr) condition(s->expr, bb, xb); else br(bb, line);
        LoopTargets lt; lt.breakBlock = xb; lt.continueBlock = sb; lt.tryDepth = m_tryStack.size(); lt.cleanupDepth = m_cleanups.size(); m_loops.push_back(lt);
        m_block = bb; scopedStmt(s->kids[0], line); br(sb, line);
        m_loops.pop_back();
        m_block = sb; if (s->expr2) { value(s->expr2); endFullExpr(line); } br(cb, line);
        m_block = xb;
        closeScope(line);
        return;
    }
    case cxx::Stmt::Switch: {
        int v;
        const sema::Type* st;   // the controlling value's type
        if (s->decl) {          // "switch (int y = ...)": y lives through the body
            openScope();
            v = conditionDecl(s->decl, st, line);
            endFullExpr(line);
        } else {
            v = value(s->expr);
            st = exprType(s->expr);
            endFullExpr(line);
            openScope();
        }
        const sema::Type* vt = m_semaTypes.unqualified(m_semaTypes.decay(st));
        if (!vt->isIntegral()) vt = m_semaTypes.basic(sema::Type::Int);
        cxx::Stmt* body = s->kids[0];
        std::vector<cxx::Stmt*> items;
        if (body->kind == cxx::Stmt::Compound) items = body->kids; else items.push_back(body);
        // nested labels: "case 1: case 2: stmt" are chains of Case statements
        std::vector<cxx::Stmt*> flat;
        for (size_t i = 0; i < items.size(); ++i) {
            cxx::Stmt* it = items[i];
            while (it && (it->kind == cxx::Stmt::Case || it->kind == cxx::Stmt::Default)) { flat.push_back(it); it = it->kids.empty() ? 0 : it->kids[0]; }
            if (it) flat.push_back(it);
        }
        int xb = m_out->newBlock("switch.end");
        std::map<const cxx::Stmt*, int> labelBlock;
        int defaultBlock = -1;
        for (size_t i = 0; i < flat.size(); ++i) {
            if (flat[i]->kind == cxx::Stmt::Case) labelBlock[flat[i]] = m_out->newBlock("case");
            else if (flat[i]->kind == cxx::Stmt::Default) { defaultBlock = m_out->newBlock("default"); labelBlock[flat[i]] = defaultBlock; }
        }
        for (size_t i = 0; i < flat.size(); ++i) {
            if (flat[i]->kind != cxx::Stmt::Case) continue;
            // the label's value in its own type, then converted ("case 4294967295U:" of an unsigned long long)
            const sema::Type* lt = m_semaTypes.unqualified(m_semaTypes.unref(exprType(flat[i]->expr)));
            int cv = lt && lt->isIntegral() ? convert(constant(flat[i]->expr->semaValue, lt, flat[i]->loc.line), lt, vt, flat[i]->loc.line)
                                            : constant(flat[i]->expr->semaValue, vt, flat[i]->loc.line);
            IrInst c = make(IrInst::Cmp, m_semaTypes.basic(sema::Type::Bool), flat[i]->loc.line); c.sub = "eq";
            c.args.push_back(IrOperand::temp(convert(v, st, vt, line), oldType(vt))); c.args.push_back(IrOperand::temp(cv, oldType(vt)));
            int cond = emit(c);
            int next = m_out->newBlock("switch.test");
            condbr(cond, labelBlock[flat[i]], next, flat[i]->loc.line);
            m_block = next;
        }
        br(defaultBlock >= 0 ? defaultBlock : xb, line);
        LoopTargets lt; lt.breakBlock = xb; lt.continueBlock = -1; lt.tryDepth = m_tryStack.size(); lt.cleanupDepth = m_cleanups.size(); m_loops.push_back(lt);
        bool inLabel = false;
        for (size_t i = 0; i < flat.size(); ++i) {
            if (flat[i]->kind == cxx::Stmt::Case || flat[i]->kind == cxx::Stmt::Default) {
                int b = labelBlock[flat[i]];
                if (inLabel) br(b, flat[i]->loc.line);
                m_block = b; inLabel = true;
                continue;
            }
            if (!inLabel) { m_block = m_out->newBlock("switch.dead"); inLabel = true; }
            stmt(flat[i]);
        }
        br(xb, line);
        m_loops.pop_back();
        m_block = xb;
        closeScope(line);
        return;
    }
    case cxx::Stmt::Case: case cxx::Stmt::Default: for (size_t i = 0; i < s->kids.size(); ++i) stmt(s->kids[i]); return;
    case cxx::Stmt::Break: if (!m_loops.empty()) { const LoopTargets& lt = m_loops.back(); leaveScopes(lt.cleanupDepth, line); leaveTryRegions(lt.tryDepth, line); br(lt.breakBlock, line); } return;
    case cxx::Stmt::Continue: for (size_t i = m_loops.size(); i > 0; --i) if (m_loops[i - 1].continueBlock >= 0) { leaveScopes(m_loops[i - 1].cleanupDepth, line); leaveTryRegions(m_loops[i - 1].tryDepth, line); br(m_loops[i - 1].continueBlock, line); return; } return;
    case cxx::Stmt::Return: {
        IrInst in = make(IrInst::Ret, 0, line);
        const sema::Type* rt = m_fn->type ? m_fn->type->inner : m_semaTypes.basic(sema::Type::Void);
        if (s->expr) {
            if (isReference(rt)) {
                LValue lv = lvalue(s->expr);
                int addr = lv.kind == LValue::None ? value(s->expr) : addressOf(lv, line);
                addr = convert(addr, m_semaTypes.pointer(lv.kind == LValue::None ? m_semaTypes.unref(exprType(s->expr)) : lv.type), m_semaTypes.pointer(m_semaTypes.unref(rt)), line);
                in.args.push_back(IrOperand::temp(addr, oldType(m_semaTypes.pointer(rt->inner))));
            } else if (isClassType(rt) && m_nrvo && namedLocal(s->expr) == m_nrvo) {
                // NRVO: the object already is the result
            } else if (isClassType(rt)) {
                // build the result in the hidden result object
                IrInst ld = make(IrInst::LoadLocal, m_semaTypes.pointer(rt), line); ld.args.push_back(IrOperand::local(m_sretSlot, oldType(m_semaTypes.pointer(rt))));
                materialize(s->expr, rt, emit(ld), line);
            } else if (mpKind(rt) == 3) {
                // a pointer to member in memory: converted, then copied into the result object (its bytes go in rax
                // when it has 8, abiResult)
                const int v = convert(value(s->expr), exprType(s->expr), rt, line);
                IrInst ld = make(IrInst::LoadLocal, m_semaTypes.pointer(rt), line); ld.args.push_back(IrOperand::local(m_sretSlot, oldType(m_semaTypes.pointer(rt))));
                copyBytes(emit(ld), v, sizeOf(m_semaTypes.unqualified(rt)), line);
            } else if (!rt->isVoid()) {
                int v = convert(value(s->expr), exprType(s->expr), rt, line);
                in.args.push_back(IrOperand::temp(v, oldType(m_semaTypes.unqualified(rt))));
            } else value(s->expr);
        }
        endFullExpr(line);
        returnSequence(line);
        abiResult(in, line);
        emit(in);
        return;
    }
    case cxx::Stmt::Goto: {
        if (!m_handlerLabels.empty() && !m_handlerLabels.back().count(s->label))
            unsupported(line, "a goto out of a catch handler ('" + s->label + "') is not supported yet by --compile6");
        std::map<std::string, JumpState>::const_iterator lab = m_labelStates.find(s->label);
        if (lab != m_labelStates.end()) { gotoExit(jumpState(m_block, line), lab->second, s->label); return; }
        // forward: the scopes left are known at the label; this block leaves them then
        int exitBlock = m_out->newBlock("goto." + s->label);
        br(exitBlock, line);
        m_pendingGotos[s->label].push_back(jumpState(exitBlock, line));
        return;
    }
    case cxx::Stmt::Label: {
        int b = labelBlock(s->label);
        br(b, line);
        m_block = b;
        JumpState here = jumpState(b, line);
        m_labelStates[s->label] = here;
        std::map<std::string, std::vector<JumpState> >::iterator pg = m_pendingGotos.find(s->label);
        if (pg != m_pendingGotos.end()) {
            std::vector<Cleanup> savedCleanups = m_cleanups;
            std::vector<int> savedTry = m_tryStack;
            for (size_t g = 0; g < pg->second.size(); ++g) {
                const JumpState& from = pg->second[g];
                m_block = from.block; m_cleanups = from.cleanups; m_tryStack = from.tryStack;
                gotoExit(from, here, s->label);
            }
            m_cleanups = savedCleanups; m_tryStack = savedTry;
            m_block = b;
            m_pendingGotos.erase(pg);
        }
        for (size_t i = 0; i < s->kids.size(); ++i) stmt(s->kids[i]);
        return;
    }
    case cxx::Stmt::Try: {
        m_out->hasTry = true;
        int id = m_tryCount++;
        std::vector<int> handlers;
        for (size_t i = 1; i < s->kids.size(); ++i) handlers.push_back(m_out->newBlock("catch"));
        int join = m_out->newBlock("try.end");
        int body = m_out->newBlock("try.body");
        IrInst tb = make(IrInst::TryBegin, 0, line);
        tb.extra = id;
        for (size_t i = 0; i < handlers.size(); ++i) tb.args.push_back(IrOperand::block(handlers[i]));
        emit(tb);
        br(body, line);
        m_block = body;
        size_t mark = m_flagged.size();   // objects built inside the region are destroyed at a handler entry
        m_tryStack.push_back(id);
        // a constructor's or destructor's function-try-block: the bases and members are built (or registered for
        // destruction) inside the region, so a handler entry destroys the ones built so far (15.3/10-11)
        const bool functionTry = m_functionTryDef && s == m_functionTryDef->body;
        if (functionTry) { if (m_fn->isConstructor) constructorInit(m_fn, m_functionTryDef); else destructorParts(m_fn); }
        stmt(s->kids[0]);
        m_tryStack.pop_back();
        if (!terminated()) { IrInst te = make(IrInst::TryEnd, 0, line); te.extra = id; emit(te); }
        br(join, line);
        for (size_t i = 1; i < s->kids.size(); ++i) {
            cxx::Stmt* h = s->kids[i];
            m_block = handlers[i - 1];
            IrInst cp = make(IrInst::CatchParam, m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void)), h->loc.line);
            Symbol* var = 0;
            if (h->decl && !h->decl->declarators.empty() && h->decl->declarators[0].decl) var = h->decl->declarators[0].decl->semaSym;
            const sema::Type* ct = var ? m_semaTypes.unqualified(m_semaTypes.unref(var->type)) : 0;
            if (h->catchAll) cp.sub = "...";
            else if (ct && ct->kind == sema::Type::Class && ct->decl) cp.sub = ct->decl->qualifiedName();
            else if (ct) cp.sub = ct->text();
            else if (h->decl) cp.sub = h->decl->spec.text();
            int ev = emit(cp);
            handlerCleanups(mark, h->loc.line);
            // the handler's objects (its parameter, then the body's) are destroyed before the exception it handles
            // is released, also when an exception leaves the handler (its landing pad below), as cl does
            const size_t hmark = m_flagged.size();
            openScope();   // the handler's object of class type: destroyed when the handler ends
            if (var) {
                if (ct->kind == sema::Type::Class && ct->decl && !isReference(var->type)) {
                    // caught by value: copy-constructed from the thrown object, as cl does (C++98 15.3/16)
                    int slot = newLocal(var->name, var->type, true);
                    m_slots[var] = slot;
                    LValue lv; lv.kind = LValue::Local; lv.local = slot; lv.type = ct;
                    // a copy constructor that throws here calls terminate (C++98 15.5.1), as cl does
                    int term = -1;
                    const int tid = beginTerminateRegion(term, h->loc.line);
                    copyConstruct(ct, addressOf(lv, h->loc.line), ev, h->loc.line);
                    endTerminateRegion(tid, term, h->loc.line);
                    if (needsDtor(ct)) registerCleanup(slot, ct, false, 0, h->loc.line);
                } else if (ct->kind != sema::Type::Class && !isReference(var->type)) {
                    int slot = newLocal(var->name, var->type, false);
                    m_slots[var] = slot;
                    IrInst ld = make(IrInst::LoadMem, ct, h->loc.line); ld.args.push_back(IrOperand::temp(ev, oldType(m_semaTypes.pointer(ct))));
                    int v = emit(ld);
                    LValue lv; lv.kind = LValue::Local; lv.local = slot; lv.type = ct;
                    store(lv, v, ct, h->loc.line);
                } else {
                    // the handler variable refers to the runtime's copy
                    const sema::Type* refT = m_semaTypes.reference(ct);
                    int slot = newLocal(var->name, refT, false);
                    m_slots[var] = slot;
                    var->type = refT;
                    IrInst st = make(IrInst::StoreLocal, refT, h->loc.line); st.args.push_back(IrOperand::local(slot, oldType(refT))); st.args.push_back(IrOperand::temp(ev, oldType(m_semaTypes.pointer(ct)))); emit(st);
                }
            }
            // an exception that leaves the handler ends the one it handles: the body is a region of its own, whose
            // landing pad has the runtime release the handled exception (unless it is the one leaving, a "throw;")
            // and passes the new one on
            const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
            const int handledSlot = newLocal("catch.handled", vp, false);
            { LValue hs; hs.kind = LValue::Local; hs.local = handledSlot; hs.type = vp; store(hs, ev, vp, h->loc.line); }
            const int hid = m_tryCount++;
            const int unwind = m_out->newBlock("catch.unwind");
            const int hbody = m_out->newBlock("catch.body");
            IrInst htb = make(IrInst::TryBegin, 0, h->loc.line);
            htb.extra = hid;
            htb.args.push_back(IrOperand::block(unwind));
            emit(htb);
            br(hbody, h->loc.line);
            m_block = hbody;
            m_tryStack.push_back(-1);
            m_tryStack.push_back(hid);
            {
                // a goto out of a handler would skip the release of its exception: refused (S132)
                std::set<std::string> labels;
                collectLabels(h->kids[0], labels);
                m_handlerLabels.push_back(labels);
            }
            stmt(h->kids[0]);
            m_handlerLabels.pop_back();
            // 15.3/16: reaching the end of a handler of a constructor's or destructor's function-try-block
            // rethrows (as a "throw;" there: the handler's objects are destroyed by its landing pad)
            if (functionTry && !terminated()) emit(make(IrInst::Throw, m_semaTypes.basic(sema::Type::Void), h->loc.line));
            closeScope(h->loc.line);
            m_tryStack.pop_back();
            m_tryStack.pop_back();
            if (!terminated()) {
                IrInst hte = make(IrInst::TryEnd, 0, h->loc.line);
                hte.extra = hid;
                emit(hte);
                emit(make(IrInst::CatchEnd, 0, h->loc.line));
            }
            br(join, h->loc.line);
            m_block = unwind;
            IrInst ucp = make(IrInst::CatchParam, vp, h->loc.line);
            ucp.sub = "...";
            emit(ucp);
            handlerCleanups(hmark, h->loc.line);
            IrInst ld = make(IrInst::LoadLocal, vp, h->loc.line);
            ld.args.push_back(IrOperand::local(handledSlot, oldType(vp)));
            const int handled = emit(ld);
            IrInst rel = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), h->loc.line);
            rel.sub = "cppparse_catch_unwind";
            rel.args.push_back(IrOperand::temp(handled, oldType(vp)));
            emit(rel);
            emit(make(IrInst::Throw, m_semaTypes.basic(sema::Type::Void), h->loc.line));
        }
        m_block = join;
        return;
    }
    case cxx::Stmt::Handler: return;
    }
}

// ---- functions -----------------------------------------------------------------------------------------------

void IrGen::need(Symbol* fn)
{
    if (!fn || fn->kind != Symbol::Function || m_done.count(fn)) return;
    if (fn->instantiationFailed) {          // never generated: its body did not analyse
        if (std::find(m_failedUsed.begin(), m_failedUsed.end(), fn) == m_failedUsed.end()) m_failedUsed.push_back(fn);
        return;
    }
    if (fn->isImplicit || (fn->hasBody && fn->definition)) m_queue.push_back(fn);
}

// ---- RTTI: cl's descriptors, read by vcruntime's __RTtypeid, __RTDynamicCast and __RTCastToVoid ---------------
//
// Every polymorphic class gets, as cl emits them (COMDATs, one per program): a type descriptor "??_R0" (vcruntime's
// type_info vftable, the undecorated name filled in by name(), the decorated name ".?AUD@@"), a class hierarchy
// descriptor "??_R3" whose base class array "??_R2" lists the class and every base, direct and indirect, depth
// first in declaration order (a virtual base on each path it is reached by), each a base class descriptor "??_R1"
// with the base's displacement, and per vftable a complete object locator "??_R4" (the vfptr's offset in the
// complete object, the type descriptor, the hierarchy) stored in the 8 bytes before the table. typeid of a type
// or of a non-polymorphic expression is the address of the type descriptor; typeid of a polymorphic lvalue and
// dynamic_cast across or down the hierarchy call vcruntime with the subobject that holds the first vfptr.

namespace {

IrDataItem dataInt(long v, int size) { IrDataItem it; it.kind = IrDataItem::Int; it.size = size; it.value = v; return it; }
IrDataItem dataRva(const std::string& name) { IrDataItem it; it.kind = IrDataItem::Rva; it.size = 4; it.text = name; return it; }
IrDataItem dataAddress(const std::string& name) { IrDataItem it; it.kind = IrDataItem::Address; it.size = 8; it.text = name; return it; }
IrDataItem dataBytes(const std::string& bytes) { IrDataItem it; it.kind = IrDataItem::Bytes; it.size = static_cast<int>(bytes.size()); it.text = bytes; return it; }
IrDataItem dataZero(int n) { IrDataItem it; it.kind = IrDataItem::Zero; it.size = n; return it; }

// an IR global name from a decorated or qualified name: letters, digits and underscores only
std::string rttiKey(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) out += isalnum(static_cast<unsigned char>(s[i])) ? s[i] : '_';
    return out;
}

struct RttiBase { sema::Symbol* cls; long mdisp, pdisp, vdisp, attr; size_t contained; };

// the base class array of top: cls at (mdisp, pdisp, vdisp), then each of its bases depth first; a virtual base
// is located through top's vbtable (pdisp: the vbptr, vdisp: the base's entry), a non-virtual one inside it by
// mdisp from there; contained: the entries of the subtree after it
void rttiBases(sema::Symbol* top, sema::Symbol* cls, long mdisp, long pdisp, long vdisp, std::vector<RttiBase>& out, int depth)
{
    if (!cls || depth > 32) return;
    const size_t self = out.size();
    RttiBase me; me.cls = cls; me.mdisp = mdisp; me.pdisp = pdisp; me.vdisp = vdisp; me.attr = 0x40; me.contained = 0;
    out.push_back(me);
    for (size_t b = 0; b < cls->bases.size(); ++b) {
        const sema::BaseClass& bc = cls->bases[b];
        if (!bc.cls) continue;
        if (bc.isVirtual) {
            long entry = 0;
            for (size_t t = 0; t < top->vbtables.size(); ++t)
                if (top->vbtables[t].offset == top->vbptrOffset)
                    for (size_t k = 0; k < top->vbtables[t].order.size(); ++k) if (top->vbtables[t].order[k] == bc.cls) entry = 4 * static_cast<long>(k + 1);
            const size_t at = out.size();
            rttiBases(top, bc.cls, 0, top->vbptrOffset, entry, out, depth + 1);
            if (at < out.size()) out[at].attr |= 0x10;   // BCD_VBOFCONTOBJ
        } else rttiBases(top, bc.cls, mdisp + bc.offset, pdisp, vdisp, out, depth + 1);
    }
    out[self].contained = out.size() - self - 1;
}

} // namespace

// an RTTI global, once per IR name: a COMDAT, named as cl names it under --msvc-mangling
void IrGen::rttiGlobal(const std::string& name, const std::string& decorated, const std::vector<IrDataItem>& items, bool readOnly)
{
    if (!m_x86 || !m_rttiDone.insert(name).second) return;
    if (m_msvcNames && !decorated.empty()) m_x86->registerGlobalName(name, decorated);
    IrGlobal g;
    g.name = name;
    int size = 0;
    for (size_t i = 0; i < items.size(); ++i) size += items[i].size;
    g.size = size > 0 ? size : 8;
    g.isFloat = false;
    g.data = items;
    g.readOnly = readOnly;
    g.linkOnce = true;
    g.internal = false;
    g.align = readOnly ? 4 : 8;
    m_x86->addGlobal(g);
}

// the type descriptor of t (??_R0?AUD@@@8; in .data: name() caches the undecorated name in it)
std::string IrGen::typeDescriptor(const sema::Type* t)
{
    t = m_semaTypes.unqualified(m_semaTypes.unref(t));
    const std::string rttiName = msvcRttiTypeName(t);
    const std::string name = "rtti_td_" + rttiKey(rttiName.substr(1));
    if (m_rttiDone.count(name)) return name;
    std::vector<IrDataItem> items;
    items.push_back(dataAddress("rtti_type_info_vftable"));
    items.push_back(dataInt(0, 8));
    items.push_back(dataBytes(rttiName + std::string(1, '\0')));
    rttiGlobal(name, msvcMangleTypeDescriptor(t), items, false);
    return name;
}

// the class hierarchy descriptor of cls (??_R3D@@8) with its base class array and base class descriptors; the
// bases' own hierarchies too (their descriptors are referenced)
std::string IrGen::classHierarchy(sema::Symbol* cls)
{
    const std::string key = rttiKey(cls->qualifiedName());
    const std::string name = "rtti_chd_" + key;
    if (m_rttiDone.count(name)) return name;
    m_rttiDone.insert(name);
    std::vector<RttiBase> bases;
    rttiBases(cls, cls, 0, -1, 0, bases, 0);
    std::vector<IrDataItem> array;
    long attrs = 0;   // CHD_MULTINH: a class with several direct bases; CHD_VIRTINH: a virtual base reached twice; CHD_AMBIGUOUS: a non-virtual one twice
    for (size_t i = 0; i < bases.size(); ++i) {
        const RttiBase& b = bases[i];
        if (b.cls->bases.size() > 1) attrs |= 1;
        for (size_t k = 0; k < i; ++k) if (bases[k].cls == b.cls) attrs |= (b.attr & 0x10) ? 2 : 4;
        const std::string bchd = b.cls == cls ? name : classHierarchy(b.cls);
        const std::string bcd = "rtti_bcd_" + rttiKey(b.cls->qualifiedName()) + "_" + numText(b.mdisp) + "_" + numText(b.pdisp) + "_" + numText(b.vdisp) + "_" + numText(b.attr);
        std::vector<IrDataItem> items;
        items.push_back(dataRva(typeDescriptor(b.cls->type)));
        items.push_back(dataInt(static_cast<long>(b.contained), 4));
        items.push_back(dataInt(b.mdisp, 4));
        items.push_back(dataInt(b.pdisp, 4));
        items.push_back(dataInt(b.vdisp, 4));
        items.push_back(dataInt(b.attr, 4));
        items.push_back(dataRva(bchd));
        rttiGlobal(bcd, msvcMangleBaseClassDescriptor(*b.cls, b.mdisp, b.pdisp, b.vdisp, b.attr), items, true);
        array.push_back(dataRva(bcd));
    }
    array.push_back(dataZero(3));   // cl pads the array so
    const std::string bca = "rtti_bca_" + key;
    rttiGlobal(bca, msvcMangleBaseClassArray(*cls), array, true);
    std::vector<IrDataItem> chd;
    chd.push_back(dataInt(0, 4));
    chd.push_back(dataInt(attrs, 4));
    chd.push_back(dataInt(static_cast<long>(bases.size()), 4));
    chd.push_back(dataRva(bca));
    m_rttiDone.erase(name);
    rttiGlobal(name, msvcMangleClassHierarchy(*cls), chd, true);
    return name;
}

// std::__cppparse_throw_bad_cast and the like, defined by <typeinfo> (0 without it)
Symbol* IrGen::stdFunction(const char* name)
{
    const std::vector<Symbol*>* ns = m_prog.global->find("std");
    if (!ns) return 0;
    for (size_t i = 0; i < ns->size(); ++i) {
        Symbol* n = (*ns)[i];
        if (n->kind != Symbol::Namespace || !n->members) continue;
        const std::vector<Symbol*>* v = n->members->find(name);
        if (v) for (size_t k = 0; k < v->size(); ++k) if ((*v)[k]->kind == Symbol::Function && (*v)[k]->hasBody) return (*v)[k];
    }
    return 0;
}

// the subobject of a cls object at addr that holds the class's first vfptr: the object itself, or the virtual base
// it lies in (cl moves the pointer there before __RTtypeid / __RTDynamicCast read the vftable's locator)
int IrGen::vfptrSubobject(int addr, Symbol* cls, int line)
{
    if (cls->vftables.empty()) return addr;
    const sema::VfTable& v = cls->vftables[0];
    const sema::Type* chp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Char));
    const sema::Type* cp = m_semaTypes.pointer(m_semaTypes.classType(cls));
    if (v.vbase) {
        size_t vi = 0;
        while (vi < cls->vbases.size() && cls->vbases[vi] != v.vbase) ++vi;
        const int vbOff = vi < cls->vbaseOffsets.size() ? cls->vbaseOffsets[vi] : 0;
        const int base = toBase(addr, cls, v.vbase, line);
        const int rel = v.offset - vbOff;
        return rel ? offsetAddress(base, chp, chp, rel, line) : base;
    }
    return v.offset ? offsetAddress(addr, cp, chp, v.offset, line) : addr;
}

// the same for a pointer that may be null (dynamic_cast's operand): null stays null
int IrGen::vfptrSubobjectOrNull(int p, Symbol* cls, int line)
{
    if (cls->vftables.empty() || (!cls->vftables[0].vbase && cls->vftables[0].offset == 0)) return p;
    const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
    int slot = newLocal("tmp.rtti", vp, false);
    LValue lv; lv.kind = LValue::Local; lv.local = slot; lv.type = vp;
    store(lv, constant(0, vp, line), vp, line);
    int nb = m_out->newBlock("rtti.nonnull"), jb = m_out->newBlock("rtti.end");
    condbr(toBool(p, m_semaTypes.pointer(m_semaTypes.classType(cls)), line), nb, jb, line);
    m_block = nb;
    store(lv, vfptrSubobject(p, cls, line), vp, line);
    br(jb, line);
    m_block = jb;
    return load(lv, vp, line);
}

// typeid(...): the type_info object (5.2.8): of the dynamic type for an lvalue of a polymorphic class (through the
// vftable's locator; *p with p null throws bad_typeid), else of the static type, the operand unevaluated
int IrGen::typeidAddress(cxx::Expr* e, int line)
{
    const sema::Type* ot = e->semaOperand ? e->semaOperand : m_semaTypes.basic(sema::Type::Void);
    cxx::Expr* x = e->kids.empty() ? 0 : e->kids[0];
    while (x && x->kind == cxx::Expr::Paren && !x->kids.empty()) x = x->kids[0];
    const sema::Type* cu = m_semaTypes.unqualified(m_semaTypes.unref(ot));
    const sema::Type* tip = m_semaTypes.pointer(m_semaTypes.unref(exprType(e)));
    const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
    if (x && x->semaLvalue && cu->kind == sema::Type::Class && cu->decl && cu->decl->isPolymorphic) {
        int addr;
        if (x->kind == cxx::Expr::Unary && x->text == "*" && !x->semaSym && x->kids.size() == 1) {
            addr = value(x->kids[0]);
            Symbol* thrower = stdFunction("__cppparse_throw_bad_typeid");
            if (thrower) {
                int ok = m_out->newBlock("typeid.ok"), bad = m_out->newBlock("typeid.null");
                condbr(toBool(addr, m_semaTypes.decay(exprType(x->kids[0])), line), ok, bad, line);
                m_block = bad;
                std::vector<cxx::Expr*> none;
                callFunction(thrower, -1, -1, none, 0, false, line);
                br(ok, line);
                m_block = ok;
            }
        } else { LValue lv = lvalue(x); addr = lv.kind == LValue::None ? value(x) : addressOf(lv, line); }
        IrInst in = make(IrInst::Call, tip, line);
        in.sub = "__RTtypeid";
        in.args.push_back(IrOperand::temp(vfptrSubobject(addr, cu->decl, line), oldType(vp)));
        return emit(in);
    }
    IrInst in = make(IrInst::AddrGlobal, tip, line);
    in.args.push_back(IrOperand::global(typeDescriptor(cu), oldType(m_semaTypes.basic(sema::Type::Void))));
    return emit(in);
}

// dynamic_cast<T*>(p) / <T&>(l) / <void*>(p) (5.2.7) that is not a conversion to a base: vcruntime finds the complete
// object through the locator and walks its hierarchy; a failed reference cast throws bad_cast
bool IrGen::dynamicCast(cxx::Expr* e, int line, int& result)
{
    const sema::Type* to = exprType(e);
    cxx::Expr* x = e->kids[0];
    const bool ref = isReference(to);
    if (!ref && to->kind != sema::Type::Pointer) return false;
    const sema::Type* target = m_semaTypes.unqualified(to->inner);
    const sema::Type* srcT = 0;
    if (ref) srcT = m_semaTypes.unqualified(m_semaTypes.unref(exprType(x)));
    else { const sema::Type* pt = m_semaTypes.decay(m_semaTypes.unref(exprType(x))); if (pt->kind == sema::Type::Pointer) srcT = m_semaTypes.unqualified(pt->inner); }
    if (!srcT || srcT->kind != sema::Type::Class || !srcT->decl || !srcT->decl->isPolymorphic) return false;
    if (target->kind == sema::Type::Class && target->decl && (target->decl == srcT->decl || isBaseOf(srcT->decl, target->decl))) return false;
    if (target->kind != sema::Type::Class && target->kind != sema::Type::Void) return false;
    const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
    const sema::Type* chp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Char));
    const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
    int p;
    if (ref) { LValue lv = lvalue(x); p = lv.kind == LValue::None ? value(x) : addressOf(lv, line); }
    else p = value(x);
    const int in = ref ? vfptrSubobject(p, srcT->decl, line) : vfptrSubobjectOrNull(p, srcT->decl, line);
    if (target->kind == sema::Type::Void) {
        IrInst c = make(IrInst::Call, vp, line);
        c.sub = "__RTCastToVoid";
        c.args.push_back(IrOperand::temp(in, oldType(vp)));
        result = emit(c);
        return true;
    }
    // VfDelta: how far the vfptr's subobject lies from the pointer (0 unless the first vfptr is inside a virtual base)
    int delta = in == p ? constant(0, it, line) : convert(binary("-", in, chp, p, chp, m_semaTypes.basic(sema::Type::LongLong), line), m_semaTypes.basic(sema::Type::LongLong), it, line);
    IrInst sd = make(IrInst::AddrGlobal, vp, line); sd.args.push_back(IrOperand::global(typeDescriptor(srcT), oldType(m_semaTypes.basic(sema::Type::Void))));
    IrInst td = make(IrInst::AddrGlobal, vp, line); td.args.push_back(IrOperand::global(typeDescriptor(target), oldType(m_semaTypes.basic(sema::Type::Void))));
    const int src = emit(sd), dst = emit(td);
    const sema::Type* rp = m_semaTypes.pointer(target);
    IrInst c = make(IrInst::Call, rp, line);
    c.sub = "__RTDynamicCast";
    c.args.push_back(IrOperand::temp(in, oldType(vp)));
    c.args.push_back(IrOperand::temp(delta, oldType(it)));
    c.args.push_back(IrOperand::temp(src, oldType(vp)));
    c.args.push_back(IrOperand::temp(dst, oldType(vp)));
    c.args.push_back(IrOperand::temp(constant(0, it, line), oldType(it)));
    int r = emit(c);
    if (ref) {
        Symbol* thrower = stdFunction("__cppparse_throw_bad_cast");
        if (thrower) {
            int ok = m_out->newBlock("dyncast.ok"), bad = m_out->newBlock("dyncast.bad");
            condbr(toBool(r, rp, line), ok, bad, line);
            m_block = bad;
            std::vector<cxx::Expr*> none;
            callFunction(thrower, -1, -1, none, 0, false, line);
            br(ok, line);
            m_block = ok;
        }
    }
    result = r;
    return true;
}

// the IR name of a class's vftable t (0: the one at offset 0, "vtable@D"; then "vtable@D{for PB@16}")
std::string IrGen::vtableName(const Symbol* cls, size_t t)
{
    std::string n = "vtable@" + cls->qualifiedName();
    if (t == 0 || t >= cls->vftables.size()) return n;
    const sema::VfTable& v = cls->vftables[t];
    return n + "{for " + (v.introducedBy ? v.introducedBy->qualifiedName() : std::string("?")) + "@" + numText(v.offset) + "}";
}

// every vfptr of the object: the one at offset 0, those of the secondary vftables at their offsets, and those
// inside the virtual bases, found through the vbtable (a constructor may be building a base subobject of a bigger
// object, where the virtual base lies elsewhere); with it cl's vtordisp before such a base: how far the base lies
// from where this class's layout has it
void IrGen::storeVptr(Symbol* cls, int objAddr, int line)
{
    if (!cls->isPolymorphic) return;
    const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
    const sema::Type* cp = m_semaTypes.pointer(m_semaTypes.classType(cls));
    const sema::Type* chp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Char));
    const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
    const size_t n = cls->vftables.empty() ? 1 : cls->vftables.size();
    for (size_t t = 0; t < n; ++t) {
        IrInst vt = make(IrInst::AddrGlobal, vp, line);
        vt.args.push_back(IrOperand::global(vtableName(cls, t), oldType(m_semaTypes.basic(sema::Type::Void))));
        int vtp = emit(vt);
        int at = objAddr;
        const sema::VfTable* v = cls->vftables.empty() ? 0 : &cls->vftables[t];
        if (v && v->vbase) {
            size_t vi = 0;
            while (vi < cls->vbases.size() && cls->vbases[vi] != v->vbase) ++vi;
            const int vbOff = vi < cls->vbaseOffsets.size() ? cls->vbaseOffsets[vi] : 0;
            const int base = toBase(objAddr, cls, v->vbase, line);
            const int rel = v->offset - vbOff;
            at = rel ? offsetAddress(base, chp, chp, rel, line) : base;
            if (v->vtordisp) {
                const int expected = offsetAddress(objAddr, cp, chp, vbOff, line);
                const int disp = convert(binary("-", base, chp, expected, chp, m_semaTypes.basic(sema::Type::LongLong), line), m_semaTypes.basic(sema::Type::LongLong), it, line);
                LValue dl; dl.kind = LValue::Mem; dl.addr = offsetAddress(base, chp, m_semaTypes.pointer(it), -4, line); dl.type = it;
                store(dl, disp, it, line);
            }
        } else if (v && v->offset) { IrInst fa = make(IrInst::AddrField, m_semaTypes.pointer(vp), line); fa.sub = "vptr"; fa.extra = v->offset; fa.args.push_back(IrOperand::temp(objAddr, oldType(cp))); at = emit(fa); }
        IrInst st = make(IrInst::StoreMem, vp, line);
        st.sub = "vptr";
        st.args.push_back(IrOperand::temp(at, oldType(at == objAddr ? cp : m_semaTypes.pointer(vp))));
        st.args.push_back(IrOperand::temp(vtp, oldType(vp)));
        emit(st);
    }
}

// the this a virtual call of fn loads the vfptr from: through the vbtable when fn's table lies in a virtual base of
// its class, else at its fixed offset
int IrGen::vfptrThis(Symbol* fn, int thisPtr, int line)
{
    if (fn->vfptrVbase) return toBase(thisPtr, fn->ownerClass, fn->vfptrVbase, line);
    if (!fn->vfptrOffset) return thisPtr;
    const sema::Type* cp = m_semaTypes.pointer(m_semaTypes.classType(fn->ownerClass));
    return offsetAddress(thisPtr, cp, cp, fn->vfptrOffset, line);
}

// the IR name of a class's vbtable t ("vbtable@D", "vbtable@D{for VL@16}", "vbtable@R{for B/P@0}")
std::string IrGen::vbtableName(const Symbol* cls, size_t t)
{
    std::string n = "vbtable@" + cls->qualifiedName();
    if (t >= cls->vbtables.size() || cls->vbtables[t].path.empty()) return n;
    const sema::VbTable& v = cls->vbtables[t];
    n += "{for ";
    for (size_t p = 0; p < v.path.size(); ++p) n += (p ? "/" : "") + v.path[p]->qualifiedName();
    return n + "@" + numText(v.offset) + "}";
}

// every vbptr of a complete object (MSVC: stored by the constructor that has the "most derived" flag)
void IrGen::storeVbptrs(Symbol* cls, int objAddr, int line)
{
    const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
    const sema::Type* cp = m_semaTypes.pointer(m_semaTypes.classType(cls));
    for (size_t t = 0; t < cls->vbtables.size(); ++t) {
        IrInst vt = make(IrInst::AddrGlobal, vp, line);
        vt.args.push_back(IrOperand::global(vbtableName(cls, t), oldType(m_semaTypes.basic(sema::Type::Int))));
        int vtp = emit(vt);
        IrInst fa = make(IrInst::AddrField, m_semaTypes.pointer(vp), line); fa.sub = "vbptr"; fa.extra = cls->vbtables[t].offset; fa.args.push_back(IrOperand::temp(objAddr, oldType(cp)));
        int at = emit(fa);
        IrInst st = make(IrInst::StoreMem, vp, line);
        st.sub = "vbptr";
        st.args.push_back(IrOperand::temp(at, oldType(m_semaTypes.pointer(vp))));
        st.args.push_back(IrOperand::temp(vtp, oldType(vp)));
        emit(st);
    }
}

// cl's vbase destructor (??_DD@@QEAAXXZ): ~D, then the virtual bases in the reverse of their order
Symbol* IrGen::vbaseDestructor(Symbol* cls)
{
    std::map<Symbol*, Symbol*>::iterator it = m_vbaseDtors.find(cls);
    if (it != m_vbaseDtors.end()) return it->second;
    Symbol* fn = m_prog.newSymbol(Symbol::Function, "`vbase destructor'", cls->members);
    fn->ownerClass = cls;
    fn->isImplicit = true;                 // one copy survives linking, as cl's COMDAT
    fn->hasBody = true;
    fn->loc = cls->loc;
    fn->type = m_semaTypes.function(m_semaTypes.basic(sema::Type::Void), std::vector<const sema::Type*>(), false, false);
    m_vbaseDtors[cls] = fn;
    m_vbaseDtorOf[fn] = cls;
    if (m_msvcNames && m_x86) {
        Symbol* dtor = findSpecial(cls, '~');
        const std::string d = dtor ? msvcMangleSymbol(*dtor) : std::string();   // ??1D@@QEAA@XZ
        const size_t at = d.find("@@");
        if (d.compare(0, 3, "??1") == 0 && at != std::string::npos) m_x86->registerSymbolName(irName(fn), "??_D" + d.substr(3, at - 3) + "@@QEAAXXZ");
    }
    return fn;
}

void IrGen::genVbaseDestructor(Symbol* fn)
{
    Symbol* cls = m_vbaseDtorOf[fn];
    const int line = fn->loc.line;
    const int self = thisTemp(line);
    if (Symbol* dtor = findSpecial(cls, '~')) callSpecial(dtor, self, -1, line);
    const sema::Type* cp = m_semaTypes.pointer(m_semaTypes.classType(cls));
    for (size_t v = cls->vbases.size(); v > 0; --v) {
        Symbol* vb = cls->vbases[v - 1];
        if (!needsDtor(vb->type)) continue;
        destroy(vb->type, offsetAddress(self, cp, m_semaTypes.pointer(vb->type), cls->vbaseOffsets[v - 1], line), line, false, false);
    }
}

void IrGen::setFiles(const std::vector<std::string>& files)
{
    // as cl records them: absolute paths with backslashes (a debugger opens the file from the line table alone)
    m_files.clear();
    for (size_t i = 0; i < files.size(); ++i) {
        std::string p = normalizePath(absolutePath(files[i]));
        for (size_t k = 0; k < p.size(); ++k) if (p[k] == '/') p[k] = '\\';
        m_files.push_back(p);
    }
}

void IrGen::prologue(Symbol* fn)
{
    m_nrvo = 0;
    m_out->name = irName(fn);
    m_out->fn = 0;
    // the source file and line of the definition (of the declaration for an implicit member), for the line table
    const cxx::Loc& where = fn->definition ? fn->definition->loc : fn->loc;
    m_out->sourceFile = where.file >= 0 && static_cast<size_t>(where.file) < m_files.size() ? m_files[static_cast<size_t>(where.file)] : std::string();
    m_out->sourceLine = where.line;
    m_out->variadic = fn->type && fn->type->kind == sema::Type::Function && fn->type->variadic;
    m_out->linkOnce = fn->isInline || fn->isImplicit || fn->instantiatedFrom != 0 || (fn->ownerClass && fn->ownerClass->instantiatedFrom != 0);
    m_out->internal = hasInternalLinkage(fn) || (fn->ownerClass && hasInternalLinkage(fn->ownerClass));
    if (m_out->internal) m_out->linkOnce = false;   // every unit keeps its own copy
    const sema::Type* rt = fn->type && fn->type->kind == sema::Type::Function ? fn->type->inner : m_semaTypes.basic(sema::Type::Void);
    const bool mpResult = mpKind(rt) == 3 && !isReference(rt);
    bool sret = (isClassType(rt) || mpResult) && !isReference(rt);
    m_retReg = sret ? regReturnType(fn, rt) : 0;
    m_out->retType = oldType(m_retReg ? m_retReg : sret ? m_semaTypes.basic(sema::Type::Void) : (isReference(rt) ? m_semaTypes.pointer(rt->inner) : rt));
    m_out->hasThis = fn->ownerClass && !fn->isStatic;
    m_thisSlot = 0;
    m_sretSlot = -1;
    // a member function's pointer-to-member result in memory: cl passes the hidden result pointer before this
    const bool sretFirst = sret && !m_retReg && mpResult && m_out->hasThis;
    if (sretFirst) {
        m_sretSlot = newLocal("sret", m_semaTypes.pointer(rt), false);
        m_out->locals[static_cast<size_t>(m_sretSlot)].isParam = true;
        m_out->locals[static_cast<size_t>(m_sretSlot)].nonNull = true;
    }
    if (m_out->hasThis) {
        const sema::Type* ct = m_semaTypes.classType(fn->ownerClass);
        if (fn->type && fn->type->constMethod) ct = m_semaTypes.qualified(ct, true, false);
        int slot = newLocal("this", m_semaTypes.pointer(ct), false);
        m_out->locals[static_cast<size_t>(slot)].isParam = true;
        m_out->locals[static_cast<size_t>(slot)].isThis = true;
        m_out->locals[static_cast<size_t>(slot)].nonNull = true;
        m_thisSlot = slot;
    }
    m_vbFlagSlot = -1;
    // MSVC's "most derived" flag of a constructor of a class with virtual bases (1: it builds them): the last
    // parameter, the one after this when the constructor is variadic
    const bool vbFlag = m_out->hasThis && fn->isConstructor && hasVbases(fn->ownerClass);
    if (vbFlag && m_out->variadic) {
        m_vbFlagSlot = newLocal("vb.flag", m_semaTypes.basic(sema::Type::Int), false);
        m_out->locals[static_cast<size_t>(m_vbFlagSlot)].isParam = true;
    }
    if (sret && !m_retReg && !sretFirst) {
        m_sretSlot = newLocal("sret", m_semaTypes.pointer(rt), false);
        m_out->locals[static_cast<size_t>(m_sretSlot)].isParam = true;
        m_out->locals[static_cast<size_t>(m_sretSlot)].nonNull = true;
    }
    std::vector<std::pair<int, const sema::Type*> > byValue;   // class objects the callee owns
    std::vector<Symbol*> byValueParam;                          // their parameters
    std::vector<std::pair<int, Symbol*> > inRegister;           // small class objects that arrive as an integer
    for (size_t p = 0; p < fn->params.size(); ++p) {
        Symbol* ps = fn->params[p];
        const sema::Type* pt = ps->type;
        if (const sema::Type* it = regParamType(pt)) {
            int slot = newLocal(ps->name.empty() ? "arg" + numText(static_cast<long>(p)) : ps->name + ".bits", it, false);
            m_out->locals[static_cast<size_t>(slot)].isParam = true;
            inRegister.push_back(std::make_pair(slot, ps));
            continue;
        }
        const bool object = (isClassType(pt) || mpKind(pt) == 3) && !isReference(pt);
        if (object) pt = m_semaTypes.pointer(pt);   // objects (and pointers to members in memory) arrive by address
        int slot = newLocal(ps->name.empty() ? "arg" + numText(static_cast<long>(p)) : ps->name, pt, false);
        m_out->locals[static_cast<size_t>(slot)].isParam = true;
        m_out->locals[static_cast<size_t>(slot)].nonNull = isReference(ps->type) || object;
        m_slots[ps] = slot;
        if (object) {
            byValue.push_back(std::make_pair(slot, ps->type));
            byValueParam.push_back(ps);
            ps->type = m_semaTypes.reference(ps->type);   // reads go through the pointer
        }
    }
    if (vbFlag && !m_out->variadic) {
        m_vbFlagSlot = newLocal("vb.flag", m_semaTypes.basic(sema::Type::Int), false);
        m_out->locals[static_cast<size_t>(m_vbFlagSlot)].isParam = true;
    }
    m_out->paramCount = static_cast<int>(m_out->locals.size());
    m_out->newBlock("entry");       // filled by finishFunction: flags, the unwind region, branch to the body
    m_bodyBlock = m_out->newBlock("body");
    m_block = m_bodyBlock;
    m_unwindId = m_tryCount++;
    m_tryStack.push_back(m_unwindId);
    m_cleanups.clear(); m_scopes.clear(); m_scopeIds.clear(); m_temps.clear(); m_flagged.clear(); m_dtorParts.clear(); m_labels.clear(); m_handlerLabels.clear(); m_labelStates.clear(); m_pendingGotos.clear(); m_staticCount = 0; m_guardCount = 0;
    if (m_out->hasThis && fn->thisAdjust != 0) {
        // called with the address of the vfptr whose table first holds it (MSVC's this adjustor): back to the object
        const sema::Type* tp = m_semaTypes.pointer(m_semaTypes.classType(fn->ownerClass));
        LValue tl; tl.kind = LValue::Local; tl.local = m_thisSlot; tl.type = tp;
        store(tl, offsetAddress(thisTemp(fn->loc.line), tp, tp, -fn->thisAdjust, fn->loc.line), tp, fn->loc.line);
    }
    for (size_t i = 0; i < inRegister.size(); ++i) {
        // the object lives in a local of the callee; the parameter name reaches it through a pointer, as for
        // objects passed by address
        Symbol* ps = inRegister[i].second;
        const sema::Type* ct = m_semaTypes.unqualified(m_semaTypes.unref(ps->type));
        const sema::Type* it = regParamType(ct);
        int obj = newLocal(ps->name.empty() ? "argobj" : ps->name + ".obj", ct, true);
        LValue ol; ol.kind = LValue::Local; ol.local = obj; ol.type = ct;
        int addr = addressOf(ol, fn->loc.line);
        LValue bl; bl.kind = LValue::Local; bl.local = inRegister[i].first; bl.type = it;
        LValue ml; ml.kind = LValue::Mem; ml.addr = addr; ml.type = it;
        store(ml, load(bl, it, fn->loc.line), it, fn->loc.line);
        int ptr = newLocal(ps->name.empty() ? "argptr" : ps->name, m_semaTypes.pointer(ct), false);
        m_out->locals[static_cast<size_t>(ptr)].nonNull = true;
        LValue pl; pl.kind = LValue::Local; pl.local = ptr; pl.type = m_semaTypes.pointer(ct);
        store(pl, addr, pl.type, fn->loc.line);
        m_slots[ps] = ptr;
        byValue.push_back(std::make_pair(ptr, ps->type));
        byValueParam.push_back(ps);
        ps->type = m_semaTypes.reference(ps->type);
    }
    if (m_retReg) {
        // the result is built in a local and its bytes are returned in rax (abiResult)
        int obj = newLocal("result", m_semaTypes.unqualified(rt), true);
        LValue ol; ol.kind = LValue::Local; ol.local = obj; ol.type = m_semaTypes.unqualified(rt);
        int addr = addressOf(ol, fn->loc.line);
        m_sretSlot = newLocal("sret", m_semaTypes.pointer(rt), false);
        m_out->locals[static_cast<size_t>(m_sretSlot)].nonNull = true;
        LValue pl; pl.kind = LValue::Local; pl.local = m_sretSlot; pl.type = m_semaTypes.pointer(rt);
        store(pl, addr, pl.type, fn->loc.line);
    }
    // the callee destroys its by-value parameters left to right, as cl does: registered from the last one
    for (size_t p = fn->params.size(); p-- > 0;)
        for (size_t i = 0; i < byValue.size(); ++i)
            if (byValueParam[i] == fn->params[p] && needsDtor(byValue[i].second)) registerCleanup(byValue[i].first, byValue[i].second, true, 0, fn->loc.line);
}

void IrGen::constructorInit(Symbol* fn, cxx::Decl* def)
{
    Symbol* cls = fn->ownerClass;
    if (!cls) return;
    const int line = fn->loc.line;
    int self = thisTemp(line);
    std::set<Symbol*> initialised;
    // MSVC: with the "most derived" flag set, the vbptrs are stored and the virtual bases built first, in cl's
    // order (at their places in this complete object); then the non-virtual bases, which get the flag 0
    std::vector<std::pair<Symbol*, int> > parts;
    std::vector<bool> virtualPart;
    int joinBlock = -1;
    if (hasVbases(cls) && m_vbFlagSlot >= 0) {
        const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
        LValue fl; fl.kind = LValue::Local; fl.local = m_vbFlagSlot; fl.type = it;
        int vb = m_out->newBlock("vbases");
        joinBlock = m_out->newBlock("vbases.done");
        condbr(toBool(load(fl, it, line), it, line), vb, joinBlock, line);
        m_block = vb;
        storeVbptrs(cls, self, line);
        for (size_t v = 0; v < cls->vbases.size(); ++v) { parts.push_back(std::make_pair(cls->vbases[v], cls->vbaseOffsets[v])); virtualPart.push_back(true); }
    }
    const size_t firstNonVirtual = parts.size();
    for (size_t b = 0; b < cls->bases.size(); ++b)
        if (cls->bases[b].cls && !cls->bases[b].isVirtual) { parts.push_back(std::make_pair(cls->bases[b].cls, cls->bases[b].offset)); virtualPart.push_back(false); }
    // bases named in the initializer list, in declaration order of the bases
    for (size_t p = 0; p < parts.size(); ++p) {
        if (p == firstNonVirtual && joinBlock >= 0) { br(joinBlock, line); m_block = joinBlock; }
        Symbol* base = parts[p].first;
        const int boff = parts[p].second;
        cxx::MemInit* mi = 0;
        if (def) for (size_t i = 0; i < def->memInits.size(); ++i) {
            const std::string& n = def->memInits[i].name.last();
            if (n == base->name || n == base->ctorName() || def->memInits[i].name.text() == base->qualifiedName()) mi = &def->memInits[i];
        }
        int baddr = self;
        if (boff > 0) { IrInst adj = make(IrInst::AddrField, m_semaTypes.pointer(m_semaTypes.classType(base)), line); adj.sub = "base " + base->name; adj.extra = boff; adj.args.push_back(IrOperand::temp(self, m_out->locals[static_cast<size_t>(m_thisSlot)].type)); baddr = emit(adj); }
        const std::vector<Symbol*>* ctors = base->members ? base->members->find(base->ctorName()) : 0;
        Symbol* chosen = 0;
        size_t nargs = mi ? mi->args.size() : 0;
        // the constructor overload resolution chose (sema); by the number of arguments only when there is none
        if (mi && mi->semaSym && mi->semaSym->isConstructor && mi->semaSym->ownerClass == base) chosen = mi->semaSym;
        if (ctors) for (size_t i = 0; i < ctors->size() && !chosen; ++i) { Symbol* c = (*ctors)[i]; if (c->kind == Symbol::Function && c->isConstructor && nargs >= static_cast<size_t>(c->requiredParams) && nargs <= c->params.size()) chosen = c; }
        m_ctorFlag = 0;   // a base, virtual or not, never builds its own virtual bases here
        if (mi && mi->args.empty()) valueInit(chosen, base->type, baddr, false, mi->loc.line);   // "D() : B()"
        else if (chosen) { std::vector<cxx::Expr*> none; construct(chosen, baddr, mi ? mi->args : none, 0, mi ? mi->loc.line : line); }
        else constructDefault(base->type, baddr, line);
        m_ctorFlag = 1;
        if (mi) endFullExpr(mi->loc.line);
        if (needsDtor(base->type)) {
            // destroyed when the body throws
            int part = newLocal("part." + base->name, m_semaTypes.pointer(base->type), false);
            LValue pl; pl.kind = LValue::Local; pl.local = part; pl.type = m_semaTypes.pointer(base->type);
            store(pl, baddr, pl.type, line);
            m_flagged[static_cast<size_t>(registerCleanup(part, base->type, true, 2, line))].baseSubobject = true;
        }
    }
    if (parts.size() == firstNonVirtual && joinBlock >= 0) { br(joinBlock, line); m_block = joinBlock; }
    storeVptr(cls, self, line);
    // members in declaration order
    for (size_t f = 0; f < cls->fields.size(); ++f) {
        Symbol* fld = cls->fields[f];
        cxx::MemInit* mi = 0;
        if (def) for (size_t i = 0; i < def->memInits.size(); ++i) if (def->memInits[i].name.parts.size() == 1 && def->memInits[i].name.last() == fld->name) mi = &def->memInits[i];
        IrInst fa = make(IrInst::AddrField, m_semaTypes.pointer(m_semaTypes.unref(fld->type)), line); fa.sub = fld->name; fa.extra = fld->offset; fa.args.push_back(IrOperand::temp(self, m_out->locals[static_cast<size_t>(m_thisSlot)].type));
        const sema::Type* ft = fld->type;
        if (isReference(ft)) {
            if (!mi || mi->args.empty()) continue;
            int faddr = emit(fa);
            LValue src = lvalue(mi->args[0]);
            int addr = src.kind == LValue::None ? value(mi->args[0]) : addressOf(src, mi->loc.line);
            IrInst st = make(IrInst::StoreMem, m_semaTypes.pointer(ft->inner), mi->loc.line); st.args.push_back(IrOperand::temp(faddr, oldType(m_semaTypes.pointer(ft)))); st.args.push_back(IrOperand::temp(addr, oldType(m_semaTypes.pointer(ft->inner)))); emit(st);
            continue;
        }
        const sema::Type* fu = m_semaTypes.unqualified(ft);
        if ((fu->kind == sema::Type::Class && fu->decl) || (fu->kind == sema::Type::Array && isClassType(fu->inner))) {
            int faddr = emit(fa);
            size_t nargs = mi ? mi->args.size() : 0;
            if (mi && nargs == 0) valueInit(fu->kind == sema::Type::Class && mi->semaSym && mi->semaSym->isConstructor && mi->semaSym->ownerClass == fu->decl ? mi->semaSym : 0, fu, faddr, true, mi->loc.line);
            else if (fu->kind == sema::Type::Array || !mi) constructDefault(fu, faddr, line);
            else if (nargs == 1 && isClassType(exprType(mi->args[0])) && m_semaTypes.unqualified(m_semaTypes.unref(exprType(mi->args[0]))) == fu) materialize(mi->args[0], fu, faddr, mi->loc.line);
            else {
                const std::vector<Symbol*>* ctors = fu->decl->members ? fu->decl->members->find(fu->decl->ctorName()) : 0;
                Symbol* chosen = (mi->semaSym && mi->semaSym->isConstructor && mi->semaSym->ownerClass == fu->decl) ? mi->semaSym : 0;
                if (ctors) for (size_t i = 0; i < ctors->size() && !chosen; ++i) { Symbol* c = (*ctors)[i]; if (c->kind == Symbol::Function && c->isConstructor && nargs >= static_cast<size_t>(c->requiredParams) && nargs <= c->params.size()) chosen = c; }
                if (chosen) construct(chosen, faddr, mi->args, 0, mi->loc.line);
                else constructDefault(fu, faddr, line);
            }
            if (mi) endFullExpr(mi->loc.line);
            if (needsDtor(fu)) {
                // destroyed when the body throws
                int part = newLocal("part." + fld->name, m_semaTypes.pointer(fu), false);
                LValue pl; pl.kind = LValue::Local; pl.local = part; pl.type = m_semaTypes.pointer(fu);
                store(pl, faddr, pl.type, line);
                registerCleanup(part, fu, true, 2, line);
            }
            continue;
        }
        if (!mi) continue;
        int faddr = emit(fa);
        LValue fl; fl.kind = LValue::Mem; fl.addr = faddr; fl.type = fu;
        bitField(fl, fld);
        if (mi->args.empty()) {
            // member(): value-initialised (an array of scalars, a member pointer: valueInit)
            if (fu->kind == sema::Type::Array || sema::TypeTable::holdsMemberPointer(fu)) valueInit(0, fu, faddr, true, mi->loc.line);
            else store(fl, constant(0, fu, mi->loc.line), fu, mi->loc.line);
            continue;
        }
        store(fl, convert(value(mi->args[0]), exprType(mi->args[0]), fu, mi->loc.line), fu, mi->loc.line);
        endFullExpr(mi->loc.line);
    }
    // a member of an anonymous union or struct named in the initializer list ("X() : tag(1), iv(0)")
    if (def && cls->members) for (size_t i = 0; i < cls->members->order.size(); ++i) {
        Symbol* m = cls->members->order[i];
        if (m->kind != Symbol::Variable || !m->anonHost || isReference(m->type) || isClassType(m->type)) continue;
        cxx::MemInit* mi = 0;
        for (size_t k = 0; k < def->memInits.size(); ++k) if (def->memInits[k].name.parts.size() == 1 && def->memInits[k].name.last() == m->name) mi = &def->memInits[k];
        if (!mi) continue;
        const sema::Type* fu = m_semaTypes.unqualified(m->type);
        IrInst fa = make(IrInst::AddrField, m_semaTypes.pointer(fu), mi->loc.line); fa.sub = m->name; fa.extra = m->offset; fa.args.push_back(IrOperand::temp(self, m_out->locals[static_cast<size_t>(m_thisSlot)].type));
        LValue fl; fl.kind = LValue::Mem; fl.addr = emit(fa); fl.type = fu;
        bitField(fl, m);
        store(fl, mi->args.empty() ? constant(0, fu, mi->loc.line) : convert(value(mi->args[0]), exprType(mi->args[0]), fu, mi->loc.line), fu, mi->loc.line);
        endFullExpr(mi->loc.line);
    }
}

// a destructor's members and bases, registered before its body: when the body (or the destructor of a member)
// throws, the unwind handler destroys the ones not yet destroyed, as cl does (C++98 allows the throw)
void IrGen::destructorParts(Symbol* fn)
{
    m_dtorParts.clear();
    Symbol* cls = fn->ownerClass;
    if (!cls) return;
    const int line = fn->loc.line;
    const int self = thisTemp(line);
    for (size_t b = 0; b < cls->bases.size(); ++b) {
        Symbol* base = cls->bases[b].cls;
        if (!base || cls->bases[b].isVirtual || !needsDtor(base->type)) continue;
        int baddr = self;
        if (cls->bases[b].offset > 0) { IrInst adj = make(IrInst::AddrField, m_semaTypes.pointer(m_semaTypes.classType(base)), line); adj.sub = "base " + base->name; adj.extra = cls->bases[b].offset; adj.args.push_back(IrOperand::temp(self, m_out->locals[static_cast<size_t>(m_thisSlot)].type)); baddr = emit(adj); }
        int part = newLocal("part." + base->name, m_semaTypes.pointer(base->type), true);
        LValue pl; pl.kind = LValue::Local; pl.local = part; pl.type = m_semaTypes.pointer(base->type);
        store(pl, baddr, pl.type, line);
        const int k = registerCleanup(part, base->type, true, 2, line);
        m_flagged[static_cast<size_t>(k)].baseSubobject = true;
        m_dtorParts.push_back(k);
    }
    for (size_t f = 0; f < cls->fields.size(); ++f) {
        Symbol* fld = cls->fields[f];
        if (isReference(fld->type) || !needsDtor(fld->type)) continue;
        const sema::Type* fu = m_semaTypes.unqualified(fld->type);
        IrInst fa = make(IrInst::AddrField, m_semaTypes.pointer(fu), line); fa.sub = fld->name; fa.extra = fld->offset; fa.args.push_back(IrOperand::temp(self, m_out->locals[static_cast<size_t>(m_thisSlot)].type));
        int part = newLocal("part." + fld->name, m_semaTypes.pointer(fu), true);
        LValue pl; pl.kind = LValue::Local; pl.local = part; pl.type = m_semaTypes.pointer(fu);
        store(pl, emit(fa), pl.type, line);
        m_dtorParts.push_back(registerCleanup(part, fu, true, 2, line));
    }
}

void IrGen::destructorCleanup(Symbol* fn)
{
    Symbol* cls = fn->ownerClass;
    if (!cls) return;
    const int line = fn->loc.line;
    if (!m_dtorParts.empty()) {
        // newest first; each flag is cleared before its destructor runs (tested first after a function-try-block's
        // handler, which destroyed them before it ran and may return)
        for (size_t i = m_dtorParts.size(); i > 0; --i) { Cleanup c = m_flagged[static_cast<size_t>(m_dtorParts[i - 1])]; runCleanup(c, m_functionTryDef != 0, line); }
        return;
    }
    int self = thisTemp(line);
    for (size_t f = cls->fields.size(); f > 0; --f) {
        Symbol* fld = cls->fields[f - 1];
        if (isReference(fld->type) || !needsDtor(fld->type)) continue;
        IrInst fa = make(IrInst::AddrField, m_semaTypes.pointer(fld->type), line); fa.sub = fld->name; fa.extra = fld->offset; fa.args.push_back(IrOperand::temp(self, m_out->locals[static_cast<size_t>(m_thisSlot)].type));
        destroy(fld->type, emit(fa), line, false);
    }
    for (size_t b = cls->bases.size(); b > 0; --b) {
        Symbol* base = cls->bases[b - 1].cls;
        if (!base || cls->bases[b - 1].isVirtual) continue;   // virtual bases: the vbase destructor (??_D)
        int baddr = self;
        if (cls->bases[b - 1].offset > 0) { IrInst adj = make(IrInst::AddrField, m_semaTypes.pointer(m_semaTypes.classType(base)), line); adj.sub = "base " + base->name; adj.extra = cls->bases[b - 1].offset; adj.args.push_back(IrOperand::temp(self, m_out->locals[static_cast<size_t>(m_thisSlot)].type)); baddr = emit(adj); }
        destroy(m_semaTypes.classType(base), baddr, line, false, false);
    }
}

void IrGen::genImplicit(Symbol* fn)
{
    m_out = new IrFunction;
    m_fn = fn;
    m_slots.clear(); m_loops.clear(); m_tryStack.clear(); m_tryCount = 0;
    prologue(fn);
    const int line = fn->loc.line;
    Symbol* cls = fn->ownerClass;
    const sema::Type* ct = m_semaTypes.classType(cls);
    if (m_vbaseDtorOf.count(fn)) genVbaseDestructor(fn);
    else if (fn->isDestructor) destructorParts(fn);
    else if (fn->isConstructor && fn->params.empty()) constructorInit(fn, 0);
    else if (!fn->isDestructor) {   // an implicit destructor's body is empty: returnSequence destroys members and bases
        // copy constructor / copy assignment: memberwise, bases first
        bool assign = !fn->isConstructor;
        int self = thisTemp(line);
        const int other = 1;   // after this (the "most derived" flag is the last parameter)
        IrInst ld = make(IrInst::LoadLocal, 0, line); ld.type = m_out->locals[static_cast<size_t>(other)].type; ld.args.push_back(IrOperand::local(other, m_out->locals[static_cast<size_t>(other)].type));
        int src = emit(ld);
        if (hasVbases(cls) && !assign && m_vbFlagSlot >= 0) {
            // the most derived object's copy: vbptrs and virtual bases first (the source's found through its vbptr)
            const sema::Type* it = m_semaTypes.basic(sema::Type::Int);
            LValue fl; fl.kind = LValue::Local; fl.local = m_vbFlagSlot; fl.type = it;
            int vb = m_out->newBlock("vbases"), jb = m_out->newBlock("vbases.done");
            condbr(toBool(load(fl, it, line), it, line), vb, jb, line);
            m_block = vb;
            storeVbptrs(cls, self, line);
            for (size_t v = 0; v < cls->vbases.size(); ++v) {
                Symbol* base = cls->vbases[v];
                int d = subobject(self, ct, "vbase " + base->name, cls->vbaseOffsets[v], base->type, line);
                const int s = toBase(src, cls, base, line);
                m_ctorFlag = 0;
                copyConstruct(base->type, d, s, line);
                m_ctorFlag = 1;
            }
            br(jb, line);
            m_block = jb;
        }
        for (size_t b = 0; b < cls->bases.size(); ++b) {
            Symbol* base = cls->bases[b].cls;
            if (!base) continue;
            if (cls->bases[b].isVirtual) {
                // an assignment assigns its direct virtual bases too, as cl's implicit one does
                if (assign) copyAssign(base->type, toBase(self, cls, base, line), toBase(src, cls, base, line), line);
                continue;
            }
            int d = subobject(self, ct, "base " + base->name, cls->bases[b].offset, base->type, line);
            int s = subobject(src, ct, "base " + base->name, cls->bases[b].offset, base->type, line);
            m_ctorFlag = 0;
            if (assign) copyAssign(base->type, d, s, line); else copyConstruct(base->type, d, s, line);
            m_ctorFlag = 1;
        }
        if (!assign) storeVptr(cls, self, line);
        for (size_t f = 0; f < cls->fields.size(); ++f) {
            Symbol* fld = cls->fields[f];
            const sema::Type* ft = m_semaTypes.unref(fld->type);
            int d = subobject(self, ct, fld->name, fld->offset, ft, line);
            int s = subobject(src, ct, fld->name, fld->offset, ft, line);
            if (isReference(fld->type)) copyBytes(d, s, 8, line);
            else if (assign) copyAssign(ft, d, s, line);
            else copyConstruct(ft, d, s, line);
        }
        if (assign) {
            IrInst r = make(IrInst::Ret, 0, line);
            r.args.push_back(IrOperand::temp(thisTemp(line), m_out->locals[static_cast<size_t>(m_thisSlot)].type));
            returnSequence(line);
            emit(r);
        }
    }
    if (!terminated()) { returnSequence(line); IrInst r = make(IrInst::Ret, 0, line); abiResult(r, line); emit(r); }
    finishFunction(line);
}

// MSVC's x64 convention: a constructor returns this and a function with a hidden result pointer returns
// that pointer, both in rax; MSVC-compiled callers use them
void IrGen::abiResult(IrInst& r, int line)
{
    if (!r.args.empty() || !m_fn) return;
    if (m_fn->isConstructor && m_out->hasThis) r.args.push_back(IrOperand::temp(thisTemp(line), m_out->locals[static_cast<size_t>(m_thisSlot)].type));
    else if (m_sretSlot >= 0) {
        const IrLocal& l = m_out->locals[static_cast<size_t>(m_sretSlot)];
        IrInst ld = make(IrInst::LoadLocal, 0, line); ld.type = l.type; ld.args.push_back(IrOperand::local(m_sretSlot, l.type));
        int p = emit(ld);
        if (m_retReg) { LValue m; m.kind = LValue::Mem; m.addr = p; m.type = m_retReg; r.args.push_back(IrOperand::temp(load(m, m_retReg, line), oldType(m_retReg))); }   // the bytes in rax
        else r.args.push_back(IrOperand::temp(p, l.type));
    }
}

// cl's numbers for the blocks of a function, which name its static locals and their guards ("?1" for the body,
// 2): counted in source order from the body, a compound statement takes the next number; an if takes one, and
// each of its branches one more (the branch's scope) before its own statements; for, while, do and switch take
// one before their body; a try block and a handler are their compound statements. Found from cl's objects
// (static locals in blocks, branches, loops, switch, try/catch, labels, conditions with declarations)
void IrGen::numberStaticBlocks(cxx::Stmt* s, long& next, long current)
{
    if (!s) return;
    switch (s->kind) {
    case cxx::Stmt::Compound: {
        const long mine = next++;
        (void)current;
        for (size_t i = 0; i < s->kids.size(); ++i) numberStaticBlocks(s->kids[i], next, mine);
        return;
    }
    case cxx::Stmt::DeclStmt:
        if (s->decl)
            for (size_t i = 0; i < s->decl->declarators.size(); ++i) {
                Symbol* v = s->decl->declarators[i].decl ? s->decl->declarators[i].decl->semaSym : 0;
                if (v && v->kind == Symbol::Variable && v->isStatic) m_staticBlocks[v] = current;
            }
        return;
    case cxx::Stmt::If:
        ++next;
        for (size_t i = 0; i < s->kids.size() && i < 2; ++i) { const long branch = next++; numberStaticBlocks(s->kids[i], next, branch); }
        return;
    case cxx::Stmt::For: case cxx::Stmt::While: case cxx::Stmt::DoWhile: case cxx::Stmt::Switch: {
        const long mine = next++;
        numberStaticBlocks(s->init, next, mine);
        for (size_t i = 0; i < s->kids.size(); ++i) numberStaticBlocks(s->kids[i], next, mine);
        return;
    }
    default:
        numberStaticBlocks(s->init, next, current);
        for (size_t i = 0; i < s->kids.size(); ++i) numberStaticBlocks(s->kids[i], next, current);
        return;
    }
}

// the variable a name expression (in parentheses or not) refers to, or 0
static Symbol* namedLocal(cxx::Expr* e)
{
    while (e && e->kind == cxx::Expr::Paren && !e->kids.empty()) e = e->kids[0];
    return e && e->kind == cxx::Expr::Name && e->semaSym && e->semaSym->kind == Symbol::Variable ? e->semaSym : 0;
}

static void collectReturns(cxx::Stmt* s, std::vector<cxx::Stmt*>& rets, bool& hasGoto)
{
    if (!s) return;
    if (s->kind == cxx::Stmt::Return) rets.push_back(s);
    if (s->kind == cxx::Stmt::Goto || s->kind == cxx::Stmt::Asm) hasGoto = true;
    collectReturns(s->init, rets, hasGoto);
    for (size_t i = 0; i < s->kids.size(); ++i) collectReturns(s->kids[i], rets, hasGoto);
}

// cl /O2's named return value optimisation, for a result returned through the hidden pointer: a local of the
// result's class declared directly in the body and named by every return statement is built in the result
// object. Not with goto (a jump back over the declaration would build it twice)
Symbol* IrGen::namedResult(Symbol* fn, cxx::Stmt* body)
{
    if (!body || body->kind != cxx::Stmt::Compound || !fn->type || !fn->type->inner) return 0;
    const sema::Type* rt = m_semaTypes.unqualified(fn->type->inner);
    std::vector<cxx::Stmt*> rets;
    bool hasGoto = false;
    collectReturns(body, rets, hasGoto);
    if (rets.empty() || hasGoto) return 0;
    Symbol* var = 0;
    for (size_t i = 0; i < rets.size(); ++i) {
        Symbol* v = namedLocal(rets[i]->expr);
        if (!v || (var && v != var)) return 0;
        var = v;
    }
    if (!var->isLocal || var->isStatic || !var->type || isReference(var->type) || m_semaTypes.unqualified(var->type) != rt) return 0;
    for (size_t i = 0; i < body->kids.size(); ++i) {
        cxx::Stmt* k = body->kids[i];
        if (k->kind != cxx::Stmt::DeclStmt || !k->decl || k->decl->kind != cxx::Decl::Simple) continue;
        for (size_t d = 0; d < k->decl->declarators.size(); ++d)
            if (k->decl->declarators[d].decl && k->decl->declarators[d].decl->semaSym == var) return var;
    }
    return 0;
}

void IrGen::genFunction(Symbol* fn)
{
    cxx::Decl* def = fn->definition;
    if (!def || !def->body) return;
    m_out = new IrFunction;
    m_fn = fn;
    m_slots.clear(); m_loops.clear(); m_tryStack.clear(); m_tryCount = 0;
    prologue(fn);
    m_nrvo = (m_nrvoEnabled && m_sretSlot >= 0 && !m_retReg) ? namedResult(fn, def->body) : 0;
    // a function-try-block of a constructor or destructor (15.3): its region holds the bases and members built
    // by the initializers (destroyed before a handler runs) and every handler rethrows at its end; the Try
    // statement emits them (constructorInit / destructorParts inside its region)
    const bool functionTry = def->body->kind == cxx::Stmt::Try && (fn->isConstructor || (fn->isDestructor && fn->ownerClass));
    m_functionTryDef = functionTry ? def : 0;
    if (fn->isConstructor && !functionTry) constructorInit(fn, def);
    // as cl: a destructor stores its own class's vptrs first, so a virtual call made while it (or a base's
    // destructor after it) runs reaches this class's functions, not those of the derived object being destroyed
    if (fn->isDestructor && fn->ownerClass) storeVptr(fn->ownerClass, thisTemp(def->loc.line), def->loc.line);
    if (fn->isDestructor && fn->ownerClass && !functionTry) destructorParts(fn);
    m_staticBlocks.clear();
    // a function-try-block: the body (2) holds the try block (3, the next number) and its handlers
    if (m_msvcNames) { long next = def->body->kind == cxx::Stmt::Try ? 3 : 2; numberStaticBlocks(def->body, next, 2); }
    stmt(def->body);
    if (!terminated()) {
        IrInst r = make(IrInst::Ret, 0, def->loc.line);
        const sema::Type* rt = fn->type ? fn->type->inner : 0;
        if (rt && !rt->isVoid() && !isClassType(rt) && !isReference(rt)) {
            if (fn->name == "main") r.args.push_back(IrOperand::temp(constant(0, rt, def->loc.line), oldType(rt)));
            else r.args.push_back(IrOperand::temp(constant(0, m_semaTypes.unqualified(rt), def->loc.line), oldType(m_semaTypes.unqualified(rt))));
        }
        returnSequence(def->loc.line);
        abiResult(r, def->loc.line);
        emit(r);
    }
    finishFunction(def->loc.line);
}

void IrGen::generate(std::vector<IrFunction*>& out)
{
    m_outList = &out;
    for (size_t i = 0; i < m_prog.symbols.size(); ++i) {
        Symbol* s = m_prog.symbols[i];
        if (s->kind == Symbol::Function && s->hasBody && s->definition && !s->isDependentContext && !(s->ownerClass && s->ownerClass->isDependentContext)
            && !s->isLazyInstantiation) m_queue.push_back(s);   // template instantiations: only through need()
    }
    drainQueue();
    genDeletingDestructors();
    drainQueue();
    genInitializer();
    drainQueue();   // the implicit constructors the initialiser is the first to call (`struct Leaf : Base {}; Leaf g;`)
    // the destructor thunks call destructors that may not be generated yet, and those may build static locals
    // that register further thunks
    while (m_exitThunksDone < m_exitThunks.size()) {
        genExitThunks();
        drainQueue();
    }
}

void IrGen::drainQueue()
{
    for (size_t i = 0; i < m_queue.size(); ++i) {
        Symbol* fn = m_queue[i];
        if (m_done.count(fn)) continue;
        m_done.insert(fn);
        if (fn->isImplicit) genImplicit(fn);
        else genFunction(fn);
    }
}

// the class-specific operator delete (or delete[]) of cls (its own or a base's, as lookup finds it), or 0
Symbol* IrGen::classOperatorDelete(Symbol* cls, int depth, const char* name, size_t maxParams) const
{
    if (!cls || !cls->members || depth > 32) return 0;
    const std::vector<Symbol*>* v = cls->members->find(name);
    // the one-parameter form when the class declares both (C++98, as cl), else the sized one
    if (v) for (size_t n = 1; n <= maxParams; ++n)
        for (size_t i = 0; i < v->size(); ++i) if ((*v)[i]->kind == Symbol::Function && (*v)[i]->params.size() == n) return (*v)[i];
    for (size_t b = 0; b < cls->bases.size(); ++b) { Symbol* d = classOperatorDelete(cls->bases[b].cls, depth + 1, name, maxParams); if (d) return d; }
    return 0;
}

// frees an array from its cookie (count elements of elemSize bytes follow it), or from its first element when it has
// none: through the operator delete[] lookup finds in class cls, else (cls 0 too) the global one. A two-parameter one
// gets what cl passes: count * size, + 8 with a cookie; one element's size from a delete[] without a cookie (count
// < 0: unknown), where the global one is the unsized form
void IrGen::freeArray(Symbol* cls, int cookie, int count, int elemSize, int line, bool hasCookie)
{
    const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
    const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
    Symbol* op = cls ? classOperatorDelete(cls, 0, "operator delete[]") : 0;
    const bool sized = op ? op->params.size() == 2 : count >= 0;
    int bytes = -1;
    if (sized) {
        const int size = constant(elemSize > 0 ? elemSize : 1, lng, line);
        if (count < 0) bytes = size;
        else {
            const int elems = binary("*", count, lng, size, lng, lng, line);
            const int eight = hasCookie ? constant(8, lng, line) : -1;
            bytes = hasCookie ? binary("+", elems, lng, eight, lng, lng, line) : elems;
        }
    }
    if (op) need(op);
    IrInst in = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
    in.sub = op ? irName(op) : std::string(sized ? "__delete_array_sized" : "__delete_array");
    in.args.push_back(IrOperand::temp(cookie, oldType(vp)));
    if (bytes >= 0) in.args.push_back(IrOperand::temp(bytes, oldType(m_semaTypes.sizeType())));
    emit(in);
}

// frees an object of class cls at p through its class-specific operator delete, with the complete object's size
// for the two-parameter form, as cl does; false when the class has none (the global one frees it)
bool IrGen::freeObject(Symbol* cls, int p, int line)
{
    Symbol* op = classOperatorDelete(cls, 0);
    if (!op) return false;
    need(op);
    const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
    IrInst in = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
    in.sub = irName(op);
    in.args.push_back(IrOperand::temp(p, oldType(vp)));
    if (op->params.size() == 2) in.args.push_back(IrOperand::imm(sizeOf(cls->type), oldType(m_semaTypes.sizeType())));
    emit(in);
    return true;
}

// frees an object of size bytes through the global operator delete; cl always calls the sized form, whose library
// definition calls operator delete(void*), so replacing either one works
void IrGen::freeGlobal(int p, int size, int line)
{
    const sema::Type* vp = m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void));
    IrInst in = make(IrInst::Call, m_semaTypes.basic(sema::Type::Void), line);
    in.sub = "__delete_sized";
    in.args.push_back(IrOperand::temp(p, oldType(vp)));
    in.args.push_back(IrOperand::imm(size > 0 ? size : 1, oldType(m_semaTypes.sizeType())));
    emit(in);
}

std::string IrGen::deletingDestructor(Symbol* dtor, X86Module& x86, bool msvc)
{
    std::map<Symbol*, Symbol*>::iterator it = m_deleting.find(dtor);
    if (it != m_deleting.end()) return irName(it->second);
    Symbol* cls = dtor->ownerClass;
    Symbol* fn = m_prog.newSymbol(Symbol::Function, "`vector deleting destructor'", cls->members);
    fn->ownerClass = cls;
    fn->isImplicit = true;                 // one copy survives linking, as cl's COMDAT
    fn->hasBody = true;
    fn->loc = dtor->loc;
    const sema::Type* u = m_semaTypes.basic(sema::Type::UInt);
    fn->type = m_semaTypes.function(m_semaTypes.pointer(m_semaTypes.basic(sema::Type::Void)), std::vector<const sema::Type*>(1, u), false, false);
    Symbol* flags = m_prog.newSymbol(Symbol::Variable, "flags", 0);
    flags->type = u; flags->isParam = true;
    fn->params.push_back(flags);
    m_deleting[dtor] = fn;
    m_deletingOrder.push_back(dtor);
    if (msvc) x86.registerSymbolName(irName(fn), deletingName(msvcMangleSymbol(*dtor)));
    return irName(fn);
}

// cl's vector deleting destructor:
//   flags & 2 (delete[]): n = cookie before this; destroy this[n-1] .. this[0]; if (flags & 1) operator delete[]
//                         (cookie, n * size + 8); return cookie
//   else:                 this->~D(); if (flags & 1) operator delete(this); return this
void IrGen::genDeletingDestructors()
{
    for (; m_deletingDone < m_deletingOrder.size(); ++m_deletingDone) {
        Symbol* dtor = m_deletingOrder[m_deletingDone];
        Symbol* fn = m_deleting[dtor];
        const int line = fn->loc.line;
        m_out = new IrFunction;
        m_fn = fn;
        m_slots.clear(); m_loops.clear(); m_tryStack.clear(); m_tryCount = 0;
        prologue(fn);
        const sema::Type* u = m_semaTypes.basic(sema::Type::UInt);
        const sema::Type* lng = m_semaTypes.basic(sema::Type::LongLong);
        const sema::Type* ct = m_semaTypes.classType(dtor->ownerClass);
        const sema::Type* cp = m_semaTypes.pointer(ct);
        LValue rl; rl.kind = LValue::Local; rl.local = newLocal("tmp.result", cp, false); rl.type = cp;
        LValue fl; fl.kind = LValue::Local; fl.local = m_slots[fn->params[0]]; fl.type = u;
        const int flags0 = load(fl, u, line);      // one statement each: the order of the IR must not depend on
        const int two = constant(2, u, line);      // the host compiler's order of evaluating arguments
        const int arrayBit = binary("&", flags0, u, two, u, u, line);
        int ab = m_out->newBlock("array"), sb = m_out->newBlock("scalar"), xb = m_out->newBlock("done");
        condbr(toBool(arrayBit, u, line), ab, sb, line);

        m_block = ab;
        // this arrives at the destructor's vfptr: inside a virtual base when the class's only vfptrs lie there (cl's
        // ??_GVL2: this - 32), else the complete object
        const int first0 = thisTemp(line);
        const int first = dtor->thisAdjust ? offsetAddress(first0, cp, cp, -dtor->thisAdjust, line) : first0;
        IrInst hd = make(IrInst::AddrIndex, cp, line); hd.extra = 1;
        hd.args.push_back(IrOperand::temp(first, oldType(cp))); hd.args.push_back(IrOperand::imm(-8, oldType(lng)));
        const int cookie = emit(hd);
        LValue hl; hl.kind = LValue::Mem; hl.addr = cookie; hl.type = lng;
        const int count = load(hl, lng, line);
        forEachElement(ct, first, count, false, line);
        store(rl, cookie, cp, line);
        const int flags1 = load(fl, u, line);
        const int one1 = constant(1, u, line);
        const int freeArr = binary("&", flags1, u, one1, u, u, line);
        int fab = m_out->newBlock("delete.array");
        condbr(toBool(freeArr, u, line), fab, xb, line);
        m_block = fab;
        freeArray(dtor->ownerClass, cookie, count, sizeOf(ct), line);
        br(xb, line);

        m_block = sb;
        {
            // the complete object: with virtual bases through cl's vbase destructor (??_D)
            const int t0 = thisTemp(line);
            const int c0 = dtor->thisAdjust ? offsetAddress(t0, cp, cp, -dtor->thisAdjust, line) : t0;
            if (hasVbases(dtor->ownerClass)) callSpecial(vbaseDestructor(dtor->ownerClass), c0, -1, line);
            else callSpecial(dtor, c0, -1, line);
        }
        const int self0 = thisTemp(line);
        const int complete0 = dtor->thisAdjust ? offsetAddress(self0, cp, cp, -dtor->thisAdjust, line) : self0;
        store(rl, complete0, cp, line);
        const int flags2 = load(fl, u, line);
        const int one2 = constant(1, u, line);
        const int freeOne = binary("&", flags2, u, one2, u, u, line);
        int db = m_out->newBlock("delete");
        condbr(toBool(freeOne, u, line), db, xb, line);
        m_block = db;
        const int self1 = thisTemp(line);
        const int self = dtor->thisAdjust ? offsetAddress(self1, cp, cp, -dtor->thisAdjust, line) : self1;
        if (!freeObject(dtor->ownerClass, self, line)) freeGlobal(self, sizeOf(ct), line);
        br(xb, line);

        m_block = xb;
        returnSequence(line);
        const int result = load(rl, cp, line);
        IrInst r = make(IrInst::Ret, 0, line);
        r.args.push_back(IrOperand::temp(result, m_out->locals[static_cast<size_t>(m_thisSlot)].type));
        emit(r);
        finishFunction(line);
    }
}

void IrGen::registerMetadata(X86Module& x86, bool msvc)
{
    m_x86 = &x86;
    m_msvcNames = msvc;
    if (msvc) { x86.registerGlobalName("rtti_type_info_vftable", "??_7type_info@@6B@"); }   // vcruntime's, the first word of every type descriptor
    for (size_t i = 0; i < m_prog.symbols.size(); ++i) {
        Symbol* s = m_prog.symbols[i];
        if (s->kind == Symbol::Class && s->isDefinition && s->isPolymorphic && !s->isDependentContext && s->layoutDone) {
            // one table per vfptr; a slot whose overrider expects this elsewhere holds an adjustor thunk, named
            // as cl names it (?f@D@@WBA@EAAHXZ: D::f`adjustor{16}')
            const size_t n = s->vftables.empty() ? 1 : s->vftables.size();
            // the class each table is named for: the one that introduced its vfptr, or, when two tables share
            // that, the base on the way down to it that tells them apart (G : Q1, Q2, both from P: Q1 and Q2)
            std::vector<std::vector<Symbol*> > paths(n);
            for (size_t t = 0; t < n && n > 1; ++t) {
                const Symbol* cur = s;
                int off = s->vftables[t].offset;
                for (int guard = 0; guard < 32 && cur != s->vftables[t].introducedBy; ++guard) {
                    const sema::BaseClass* hit = 0;
                    for (size_t b = 0; b < cur->bases.size() && !hit; ++b) {
                        const sema::BaseClass& bc = cur->bases[b];
                        if (!bc.cls || !bc.cls->isPolymorphic) continue;
                        for (size_t k = 0; k < bc.cls->vftables.size(); ++k) if (bc.cls->vftables[k].offset == off - bc.offset) hit = &bc;
                    }
                    if (!hit) break;
                    paths[t].push_back(hit->cls);
                    off -= hit->offset;
                    cur = hit->cls;
                }
                if (paths[t].empty() || paths[t].back() != s->vftables[t].introducedBy) paths[t].push_back(s->vftables[t].introducedBy);
            }
            std::vector<size_t> pick(n, 0);
            for (size_t t = 0; t < n; ++t) pick[t] = paths[t].empty() ? 0 : paths[t].size() - 1;
            for (int round = 0; round < 32; ++round) {
                bool moved = false;
                for (size_t t = 0; t < n; ++t)
                    for (size_t u = t + 1; u < n; ++u)
                        if (!paths[t].empty() && !paths[u].empty() && paths[t][pick[t]] == paths[u][pick[u]]) {
                            if (pick[t] > 0) { --pick[t]; moved = true; }
                            if (pick[u] > 0) { --pick[u]; moved = true; }
                        }
                if (!moved) break;
            }
            for (size_t t = 0; t < n; ++t) {
                IrVtable v;
                const std::string name = vtableName(s, t);
                v.className = name.substr(7);
                const std::vector<Symbol*>& fns = s->vftables.empty() ? s->vtable : s->vftables[t].fns;
                for (size_t k = 0; k < fns.size(); ++k) {
                    Symbol* m = fns[k];
                    const int adjust = s->vftables.empty() ? 0 : s->vftables[t].thunks[k];
                    if (m->isPureVirtual) { v.entries.push_back("__purecall"); continue; }   // cl's slot: vcruntime's _purecall (R6025, abort)
                    need(m);
                    Symbol* root = !s->vftables.empty() && k < s->vftables[t].returnOf.size() ? s->vftables[t].returnOf[k] : 0;
                    if (root && root->type && root->type->kind == sema::Type::Function && m->type && m->type->kind == sema::Type::Function) {
                        // a covariant override's slot: cl's return-adjusting thunk, named as the function, non-virtual,
                        // returning the overridden function's type (?self@CD@@QEAAPEAUSB@@XZ)
                        const std::string thunk = irName(m) + " returning " + root->type->inner->text();
                        const int params = 1 + static_cast<int>(m->type->params.size());
                        x86.addReturnThunk(thunk, irName(m), adjust, s->vftables[t].returnAdjust[k], root->type->inner->kind == sema::Type::Reference, params > 4 ? params - 4 : 0);
                        if (msvc) x86.registerSymbolName(thunk, msvcMangleReturnThunk(*m, *root));
                        v.entries.push_back(thunk);
                        continue;
                    }
                    // the destructor's slot: the vector deleting destructor (??_ED@@UEAAPEAXI@Z), as with cl
                    const std::string target = m->isDestructor ? deletingDestructor(m, x86, msvc) : irName(m);
                    if (!s->vftables.empty() && s->vftables[t].vbase && s->vftables[t].vtordisp) {
                        // a table inside a virtual base of a class with a vtordisp for it: an entry from outside the base
                        // (an overrider) is cl's $4 thunk, which first moves this by the vtordisp field before the base
                        int dummy = 0;
                        const bool original = m->ownerClass == s->vftables[t].vbase || baseOffset(s->vftables[t].vbase, m->ownerClass, dummy);
                        if (!original) {
                            const std::string thunk = target + " vtordisp{" + numText(adjust) + "}";
                            v.entries.push_back(thunk);
                            x86.addVtordispThunk(thunk, target, adjust);
                            if (msvc) x86.registerSymbolName(thunk, m->isDestructor ? deletingName(msvcMangleVtordispThunk(*m, adjust)) : msvcMangleVtordispThunk(*m, adjust));
                            continue;
                        }
                    }
                    if (adjust == 0) { v.entries.push_back(target); continue; }
                    const std::string thunk = target + " adjustor{" + numText(adjust) + "}";
                    v.entries.push_back(thunk);
                    x86.addThunk(thunk, target, adjust);
                    if (msvc) x86.registerSymbolName(thunk, m->isDestructor ? deletingName(msvcMangleThunkSymbol(*m, adjust)) : msvcMangleThunkSymbol(*m, adjust));
                }
                // cl's RTTI locator in the 8 bytes before the table: the vfptr's offset in the complete object, the type
                // descriptor and the class hierarchy
                {
                    const Symbol* forBase = n > 1 && !paths[t].empty() ? paths[t][pick[t]] : 0;
                    const std::string col = "rtti_col_" + rttiKey(name.substr(7));
                    std::vector<IrDataItem> items;
                    items.push_back(dataInt(1, 4));
                    items.push_back(dataInt(s->vftables.empty() ? 0 : s->vftables[t].offset, 4));
                    items.push_back(dataInt(0, 4));
                    items.push_back(dataRva(typeDescriptor(s->type)));
                    items.push_back(dataRva(classHierarchy(s)));
                    items.push_back(dataRva(col));
                    rttiGlobal(col, msvcMangleCompleteObjectLocator(*s, forBase), items, true);
                    v.locator = col;
                }
                x86.addVtable(v);
                if (msvc) x86.registerGlobalName(name, msvcMangleVtableSymbol(*s, n > 1 && !paths[t].empty() ? paths[t][pick[t]] : 0));
            }
        }
        if (s->kind == Symbol::Class && s->isDefinition && !s->isDependentContext && s->layoutDone && !s->vbtables.empty()) {
            // MSVC's vbtables (??_8D@@7B@, ??_8VX@@7BVL@@@): read-only int entries, one copy kept (COMDAT)
            for (size_t t = 0; t < s->vbtables.size(); ++t) {
                IrGlobal g;
                g.name = vbtableName(s, t);
                g.size = static_cast<int>(4 * s->vbtables[t].entries.size());
                g.readOnly = true; g.linkOnce = true; g.align = 4;
                for (size_t e = 0; e < s->vbtables[t].entries.size(); ++e) { IrDataItem d; d.kind = IrDataItem::Int; d.size = 4; d.value = s->vbtables[t].entries[e]; g.data.push_back(d); }
                x86.addGlobal(g);
                if (msvc) x86.registerGlobalName(g.name, msvcMangleVbtableSymbol(*s, s->vbtables[t].path));
            }
        }
        if (s->kind == Symbol::Variable && !s->isLocal && !s->isParam && (!s->isMember || s->isStatic) && s->scope && !s->isDependentContext) {
            // a static const member with a constant initializer is folded where it is used and not emitted
            if (s->isMember && s->isStatic && s->hasConstValue && s->init && s->init->semaConst) continue;
            std::string name = s->qualifiedName();
            if (msvc) x86.registerGlobalName(name, msvcMangleVariableSymbol(*s));
            if (!s->isDefinition) continue;   // "extern T x;" only, or a static member without its out-of-line definition
            IrGlobal g;
            g.name = name;
            g.size = sizeOf(s->type) > 0 ? sizeOf(s->type) : 8;
            g.isFloat = s->type && s->type->isFloating();
            g.internal = hasInternalLinkage(s);
            // a static data member of a class template instantiation: one object for the program (14.7.1), a
            // COMDAT every unit that instantiates it defines, as cl emits it
            g.linkOnce = s->isMember && s->isStatic && s->ownerClass && s->ownerClass->isImplicitInstantiation && !g.internal;
            g.align = isReference(s->type) ? 8 : alignOf(s->type);
            if (s->declAlign > g.align) g.align = s->declAlign;
            bool dynamic = false;
            const bool scalar = !isReference(s->type) && !isClassType(s->type) && m_semaTypes.unref(s->type)->kind != sema::Type::Array;   // a reference to a constant is bound by the initialiser (its temporary)
            // an integer constant for an integer or a pointer as it is; a floating, bool or converted value through
            // constantData, which converts it exactly as cl does ("double d = 0xFFFFFFFFu;", "bool b = 0.5;")
            const sema::Type* st = m_semaTypes.unqualified(m_semaTypes.unref(s->type));
            const sema::Type* it0 = s->init ? m_semaTypes.unqualified(m_semaTypes.unref(exprType(s->init))) : 0;
            if (s->init && s->init->semaConst && scalar && st && !st->isFloating() && st->kind != sema::Type::Bool && st->kind != sema::Type::MemberPointer && it0 && (it0->isIntegral() || it0->isPointer())) g.init = numText(s->init->semaValue);
            else if (staticInit(s, g)) {}   // constants and addresses: static data, as cl emits a constant table
            else if ((isClassType(s->type) || m_semaTypes.unref(s->type)->kind == sema::Type::Array) && !isReference(s->type)) dynamic = s->initCtor != 0 || s->init != 0 || needsDefaultCtor(s->type);   // an array of classes too (`Leaf2 g2[2]`)
            else dynamic = s->init != 0;
            // zero-initialised static storage that holds pointers to members: cl writes their null values
            if (!dynamic && !s->init && g.init.empty() && g.data.empty() && !isReference(s->type)) { std::vector<IrDataItem> items; if (nullMemberPointerData(st, items, true)) g.data = items; }
            if (dynamic) m_dynamicGlobals.push_back(s);
            x86.addGlobal(g);
        }
        if (msvc && s->kind == Symbol::Function && !s->isExternC) x86.registerSymbolName(irName(s), msvcMangleSymbol(*s));
        // a function whose IR name has no parameter list (extern "C", main): its address names the function, not a variable
        if (s->kind == Symbol::Function && irName(s).find('(') == std::string::npos) x86.registerCFunction(irName(s));
    }
    if (!m_dynamicGlobals.empty()) {
        // the module's initialiser, named after its first global so that modules do not clash
        std::string base = m_dynamicGlobals[0]->qualifiedName();
        for (size_t i = 0; i < base.size(); ++i) if (!isalnum(static_cast<unsigned char>(base[i]))) base[i] = '_';
        m_initSymbol = m_prog.newSymbol(Symbol::Function, "__cppparse_init_" + base, m_prog.global);
        m_initSymbol->type = m_semaTypes.function(m_semaTypes.basic(sema::Type::Void), std::vector<const sema::Type*>(), false, false);
        m_initSymbol->isExternC = true;
        m_initSymbol->isStatic = true;   // internal: two units whose first dynamic global comes from one header would define the same name
        m_initSymbol->hasBody = true;
        x86.addInitializer(irName(m_initSymbol));
    }
}

// the module initialiser: constructs the globals that need code, in declaration order
void IrGen::genInitializer()
{
    if (!m_initSymbol) return;
    m_out = new IrFunction;
    m_fn = m_initSymbol;
    m_slots.clear(); m_loops.clear(); m_tryStack.clear(); m_tryCount = 0;
    prologue(m_initSymbol);
    for (size_t i = 0; i < m_dynamicGlobals.size(); ++i) {
        Symbol* g = m_dynamicGlobals[i];
        const int line = g->loc.line;
        LValue lv; lv.kind = LValue::Global; lv.global = g->qualifiedName(); lv.type = m_semaTypes.unref(g->type);
        cxx::InitDeclarator* id = 0;
        if (g->declAst) for (size_t k = 0; k < g->declAst->declarators.size(); ++k) if (g->declAst->declarators[k].decl && g->declAst->declarators[k].decl->semaSym == g) id = &g->declAst->declarators[k];
        if (id) initObject(lv, g->type, g, *id, line);
        else constructDefault(g->type, addressOf(lv, line), line);
        endFullExpr(line);
        if (!isReference(g->type) && needsDtor(g->type)) registerAtExit(lv.global, lv.type, line);
    }
    returnSequence(m_initSymbol->loc.line);
    emit(make(IrInst::Ret, 0, m_initSymbol->loc.line));
    finishFunction(m_initSymbol->loc.line);
}

// a global initialised by constants, string literals and addresses of globals and functions is emitted as static
// data (in .rdata when it is const), as cl does: it is valid before any initializer runs, whatever the link order
bool IrGen::staticInit(Symbol* var, IrGlobal& g)
{
    if (isReference(var->type) || needsDtor(var->type) || var->initCtor) return false;
    cxx::InitDeclarator* id = 0;
    if (var->declAst) for (size_t k = 0; k < var->declAst->declarators.size(); ++k) if (var->declAst->declarators[k].decl && var->declAst->declarators[k].decl->semaSym == var) id = &var->declAst->declarators[k];
    if (!id || id->init.kind == cxx::Initializer::None) return false;
    std::vector<IrDataItem> d;
    if (!staticData(var->type, id->init, d)) return false;
    g.data = d;
    g.readOnly = readOnlyObject(var->type);
    return true;
}

bool IrGen::staticData(const sema::Type* t, const cxx::Initializer& init, std::vector<IrDataItem>& out)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (!u) return false;
    if (init.kind == cxx::Initializer::Brace) {
        if (u->kind == sema::Type::Array || u->kind == sema::Type::Class) return aggregateData(u, &init.args, out, 0);
        return init.args.size() == 1 && constantData(t, init.args[0], out, 0);
    }
    // "const char* p = "x";", "int* p = &g;": a pointer initialised by an address; "double d = 1.0/4;",
    // "int u = int();", "float f(1.5f*2);": an arithmetic constant, folded; "char s[] = "x";"
    const bool stringArray = u->kind == sema::Type::Array && init.args.size() == 1 && init.args[0]->kind == cxx::Expr::StringLit;
    return init.args.size() == 1 && (u->kind == sema::Type::Pointer || u->kind == sema::Type::MemberPointer || u->isArithmetic() || stringArray) && constantData(t, init.args[0], out, 0);
}

// the bytes of an object of type t initialised by x (0: zero-initialised); false when the initializer needs code
bool IrGen::constantData(const sema::Type* t, cxx::Expr* x, std::vector<IrDataItem>& out, int depth)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    if (!u || depth > 16 || isReference(t)) return false;
    while (x && x->kind == cxx::Expr::Paren && x->kids.size() == 1) x = x->kids[0];
    if (x && x->kind == cxx::Expr::Error) return false;
    const int size = sizeOf(u);
    if (size <= 0) return false;
    if (u->kind == sema::Type::Array && x && x->kind == cxx::Expr::StringLit) {
        // char s[N] = "...", wchar_t w[N] = L"...": the literal's units, then zeros up to N
        const int elem = sizeOf(u->inner);
        std::vector<unsigned long> units;
        if (stringLiteralUnits(x->text, units) != (elem == 2) || (elem != 1 && elem != 2) || u->arraySize < 0) return false;
        units.push_back(0);
        long n = 0;
        for (; n < u->arraySize && n < static_cast<long>(units.size()); ++n) {
            IrDataItem it;
            it.kind = IrDataItem::Int;
            it.size = elem;
            it.value = static_cast<long>(units[static_cast<size_t>(n)]);
            out.push_back(it);
        }
        if (n < u->arraySize) zeroData(out, static_cast<int>((u->arraySize - n) * elem));
        return true;
    }
    if (u->kind == sema::Type::MemberPointer) return memberPointerData(u, x, out);
    if (u->kind == sema::Type::Array || u->kind == sema::Type::Class) {
        if (x && x->kind != cxx::Expr::InitList) return false;
        if (!x) {
            // an aggregate a brace list leaves out: zero, except that the elements of an array of pointers to
            // members are null, as cl writes them
            if (!nullMemberPointerData(u, out, false)) zeroData(out, size);
            return true;
        }
        return aggregateData(u, &x->kids, out, depth + 1);
    }
    if (x && x->kind == cxx::Expr::InitList) {   // a scalar in braces
        if (x->kids.size() > 1) return false;
        return constantData(u, x->kids.empty() ? 0 : x->kids[0], out, depth + 1);
    }
    if (!x) { zeroData(out, size); return true; }
    IrDataItem it;
    it.size = size;
    if (u->isFloating()) {
        // folded as cl folds it: "1.0/4", "1.5f*2", "double()", "(float)(1.0/3) + 1", an integer constant
        double v = 0;
        if (!foldToFloating(x, v, 0) || !convertFolded(u, v)) return false;
        // the bits as MASM's encoded real ("03FD0000000000000r"): no decimal text to round again
        unsigned char bytes[8];
        const float fv = static_cast<float>(v);
        if (size == 4) std::memcpy(bytes, &fv, 4); else if (size == 8) std::memcpy(bytes, &v, 8); else return false;
        static const char hexDigits[] = "0123456789ABCDEF";
        it.kind = IrDataItem::Float;
        it.text = "0";
        for (int i = size - 1; i >= 0; --i) { it.text += hexDigits[bytes[i] >> 4]; it.text += hexDigits[bytes[i] & 15]; }
        it.text += "r";
        out.push_back(it);
        return true;
    }
    if (u->kind == sema::Type::Pointer) {
        if (x->semaConst) {
            if (x->semaValue != 0) return false;
            zeroData(out, size);                     // the null pointer
            return true;
        }
        if (x->kind == cxx::Expr::StringLit) {
            it.kind = IrDataItem::String;
            it.text = x->text;
            out.push_back(it);
            return true;
        }
        cxx::Expr* target = x;
        const bool addressOf = x->kind == cxx::Expr::Unary && x->text == "&" && x->kids.size() == 1;
        if (addressOf) target = x->kids[0];
        while (target->kind == cxx::Expr::Paren && target->kids.size() == 1) target = target->kids[0];
        Symbol* s = target->kind == cxx::Expr::Name ? target->semaSym : 0;
        if (!s) return false;
        if (s->kind == Symbol::Function) {
            if (s->ownerClass && !s->isStatic) return false;
            need(s);
            it.text = irName(s);
        } else if (s->kind == Symbol::Variable) {
            // the address of a global, or an array naming its first element; not the value of a pointer variable
            if (isReference(s->type) || s->isParam || (s->isMember && !s->isStatic)) return false;
            if (!addressOf && m_semaTypes.unref(s->type)->kind != sema::Type::Array) return false;
            if (s->isMember && s->isStatic && s->hasConstValue && s->init && s->init->semaConst) return false;   // folded, never emitted
            if (s->isLocal) {
                std::map<Symbol*, std::string>::const_iterator sn = m_staticNames.find(s);
                if (!s->isStatic || sn == m_staticNames.end()) return false;
                it.text = sn->second;
            } else it.text = s->qualifiedName();
        } else return false;
        it.kind = IrDataItem::Address;
        out.push_back(it);
        return true;
    }
    if (!u->isIntegral()) return false;
    long v = x->semaValue;
    const sema::Type* xt = x->semaType ? m_semaTypes.unqualified(m_semaTypes.unref(x->semaType)) : 0;
    if (!x->semaConst || (xt && !xt->isIntegral())) {
        // "int()", "(int)2.75", "bool b = 0.5;", "(long long)-9.3e18": an arithmetic constant converted
        unsigned long long bits = 0;
        if (!foldInteger(x, u, bits, 0)) return false;
        const long low = static_cast<long>(static_cast<int>(static_cast<unsigned int>(bits & 0xFFFFFFFFULL)));
        if (size == 8 && static_cast<long long>(bits) != static_cast<long long>(low)) {
            // beyond what an item's sign-extended 32 bits hold: the low and the high half
            it.kind = IrDataItem::Int;
            it.size = 4;
            it.value = low;
            out.push_back(it);
            it.value = static_cast<long>(static_cast<int>(static_cast<unsigned int>(bits >> 32)));
            out.push_back(it);
            return true;
        }
        v = low;
    }
    if (u->kind == sema::Type::Bool) v = v != 0;
    else if (size == 1) v &= 0xFF;
    else if (size == 2) v &= 0xFFFF;
    it.kind = IrDataItem::Int;
    it.value = v;
    out.push_back(it);
    return true;
}

// an arithmetic constant expression, folded into v as a value of x's own type exactly as cl folds it: literals
// (a float one rounded once from its decimal), integer constants, const variables of a floating type with a
// constant initializer, unary + -, binary + - * / and conversions (casts, "T()"; to an integer modulo 2^N, see
// foldFloatToInteger). The usual arithmetic conversions apply first (float op int is float), and every
// float-typed result is rounded to float (one double operation on two floats rounded to float is the float
// operation; overflow gives infinity, as cl). Anything else not provably cl's value: false
bool IrGen::foldArith(cxx::Expr* x, double& v, int depth)
{
    if (!x || depth > 64) return false;
    const sema::Type* t = x->semaType ? m_semaTypes.unqualified(m_semaTypes.unref(x->semaType)) : 0;
    if (!t || !t->isArithmetic()) return false;
    if (t->isIntegral() && x->semaConst && !floatingCast(x)) {
        if (t->kind == sema::Type::LongLong || t->kind == sema::Type::ULongLong) {
            // semaValue is a long: only a sizeof, or a literal whose digits give that same value
            if (x->kind != cxx::Expr::SizeofType && x->kind != cxx::Expr::SizeofExpr) {
                if (x->kind != cxx::Expr::IntLit) return false;
                std::string d = x->text;
                while (!d.empty() && std::strchr("uUlL", d[d.size() - 1])) d.erase(d.size() - 1);
                double lit = 0;
                int base = 10;
                size_t i = 0;
                if (d.size() > 2 && d[0] == '0' && (d[1] == 'x' || d[1] == 'X')) { base = 16; i = 2; }
                else if (d.size() > 1 && d[0] == '0') { base = 8; i = 1; }
                for (; i < d.size(); ++i) {
                    const char ch = static_cast<char>(std::tolower(static_cast<unsigned char>(d[i])));
                    int dv = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : 99;
                    if (dv >= base) return false;
                    lit = lit * base + dv;
                }
                if (lit != static_cast<double>(x->semaValue)) return false;
            }
            if (x->semaValue < 0) return false;
        }
        v = static_cast<double>(x->semaValue);
        const bool isUnsigned = t->kind == sema::Type::UChar || t->kind == sema::Type::UShort || t->kind == sema::Type::UInt || t->kind == sema::Type::ULong;
        if (isUnsigned && v < 0) v += 4294967296.0;
        return true;
    }
    switch (x->kind) {
    case cxx::Expr::FloatLit: {
        std::string text = x->text;
        while (!text.empty() && std::strchr("fFlL", text[text.size() - 1])) text.erase(text.size() - 1);
        if (text.empty() || text.find_first_not_of("0123456789.eE+-") != std::string::npos) return false;
        v = floatingLiteralValue(text, t->kind == sema::Type::Float);
        break;
    }
    case cxx::Expr::Name: {
        // a const (not volatile) variable of a floating type initialised by a constant: "const double cd = 3e9;"
        Symbol* s = x->semaSym;
        if (!t->isFloating() || !s || s->kind != Symbol::Variable || s->isParam || !s->init || !s->type || isReference(s->type)) return false;
        if (!s->type->isConst || s->type->isVolatile) return false;
        if (!foldToFloating(s->init, v, depth + 1)) return false;
        break;
    }
    case cxx::Expr::Paren:
        if (x->kids.size() != 1 || !foldArith(x->kids[0], v, depth + 1)) return false;
        break;
    case cxx::Expr::Unary:
        if (x->kids.size() != 1 || x->semaSym || (x->text != "-" && x->text != "+") || !foldArith(x->kids[0], v, depth + 1)) return false;
        if (x->text == "-") v = -v;
        break;
    case cxx::Expr::Binary: {
        double a = 0, b = 0;
        if (x->kids.size() != 2 || x->semaSym || !foldToFloating(x->kids[0], a, depth + 1) || !foldToFloating(x->kids[1], b, depth + 1)) return false;
        if (!t->isFloating()) return false;   // integer arithmetic is sema's (semaConst)
        // the usual arithmetic conversions, from the operands' own types: double if either is, else float
        const sema::Type* ta = m_semaTypes.unqualified(m_semaTypes.unref(exprType(x->kids[0])));
        const sema::Type* tb = m_semaTypes.unqualified(m_semaTypes.unref(exprType(x->kids[1])));
        const bool wide = ta->kind == sema::Type::Double || ta->kind == sema::Type::LongDouble || tb->kind == sema::Type::Double || tb->kind == sema::Type::LongDouble;
        const sema::Type* ct = m_semaTypes.basic(wide ? sema::Type::Double : sema::Type::Float);
        if (!convertFolded(ct, a) || !convertFolded(ct, b)) return false;
        if (x->text == "+") v = a + b;
        else if (x->text == "-") v = a - b;
        else if (x->text == "*") v = a * b;
        else if (x->text == "/") { if (b == 0) return false; v = a / b; }
        else return false;
        // the operation's own type, whatever sema made of it (float op int is float: rounded here)
        if (!convertFolded(ct, v)) return false;
        break;
    }
    case cxx::Expr::Cast:
    case cxx::Expr::NamedCast:
    case cxx::Expr::FunctionalCast:
        if (x->semaSym) return false;
        if (x->kind == cxx::Expr::NamedCast && x->text != "static_cast") return false;
        if (x->kids.empty() && x->kind == cxx::Expr::FunctionalCast) { v = 0; break; }   // "int()": value-initialised
        if (x->kids.size() != 1) return false;
        if (t->isFloating() ? !foldToFloating(x->kids[0], v, depth + 1) : !foldArith(x->kids[0], v, depth + 1)) return false;
        break;
    case cxx::Expr::Cond: {
        // c ? a : b of a floating type with a constant condition: an integer constant, or a comparison of constants
        if (x->kids.size() != 3 || x->semaSym || !t->isFloating()) return false;
        cxx::Expr* c = x->kids[0];
        while (c->kind == cxx::Expr::Paren && c->kids.size() == 1) c = c->kids[0];
        bool taken = false;
        if (c->semaConst) taken = c->semaValue != 0;
        else if (c->kind == cxx::Expr::Binary && c->kids.size() == 2 && !c->semaSym
                 && (c->text == "<" || c->text == ">" || c->text == "<=" || c->text == ">=" || c->text == "==" || c->text == "!=")) {
            double a = 0, b = 0;
            if (!foldToFloating(c->kids[0], a, depth + 1) || !foldToFloating(c->kids[1], b, depth + 1)) return false;
            const sema::Type* ta = m_semaTypes.unqualified(m_semaTypes.unref(exprType(c->kids[0])));
            const sema::Type* tb = m_semaTypes.unqualified(m_semaTypes.unref(exprType(c->kids[1])));
            if (!ta->isFloating() && !tb->isFloating()) return false;
            const bool wide = ta->kind == sema::Type::Double || ta->kind == sema::Type::LongDouble || tb->kind == sema::Type::Double || tb->kind == sema::Type::LongDouble;
            const sema::Type* ct = m_semaTypes.basic(wide ? sema::Type::Double : sema::Type::Float);
            if (!convertFolded(ct, a) || !convertFolded(ct, b)) return false;
            const std::string& op = c->text;
            taken = op == "<" ? a < b : op == ">" ? a > b : op == "<=" ? a <= b : op == ">=" ? a >= b : op == "==" ? a == b : a != b;
        } else return false;
        if (!foldArith(x->kids[taken ? 1 : 2], v, depth + 1)) return false;
        break;
    }
    default:
        return false;
    }
    return convertFolded(t, v);
}

// x folded for a conversion to a floating type: a cast of a floating constant to an integer type gives its
// untruncated value there, as cl ((unsigned char)300.0 + 0.5 is 300.5, (double)(int)3e9 3000000000); through
// parentheses and a unary + that promotes nothing, while every other operation (an integral conversion, - ~,
// arithmetic, ?:) works on the value of the integer type
bool IrGen::foldToFloating(cxx::Expr* x, double& v, int depth)
{
    cxx::Expr* c = x;
    while (c && c->kids.size() == 1 && !c->semaSym && (c->kind == cxx::Expr::Paren || (c->kind == cxx::Expr::Unary && c->text == "+" && c->semaType && c->kids[0]->semaType
                                                                     && m_semaTypes.unqualified(m_semaTypes.unref(c->semaType)) == m_semaTypes.unqualified(m_semaTypes.unref(c->kids[0]->semaType)))))
        c = c->kids[0];
    cxx::Expr* cast = floatingCast(c);
    const sema::Type* ct = cast && cast->semaType ? m_semaTypes.unqualified(m_semaTypes.unref(cast->semaType)) : 0;
    if (ct && ct->isIntegral() && ct->kind != sema::Type::Bool) {
        double d = 0;
        unsigned long long bits = 0;
        return foldArith(cast->kids[0], d, depth + 1) && foldFloatToInteger(d, sizeOf(ct) * 8, ct->isUnsigned(), bits, v);
    }
    return foldArith(x, v, depth);
}

// the arithmetic constant x converted to the integral type to, exactly (64-bit values too): a floating value
// modulo 2^N as cl folds it, an integer from its own type's bits
bool IrGen::foldInteger(cxx::Expr* x, const sema::Type* to, unsigned long long& bits, int depth)
{
    if (!x || depth > 64) return false;
    while (x->kind == cxx::Expr::Paren && x->kids.size() == 1) x = x->kids[0];
    const sema::Type* xt = x->semaType ? m_semaTypes.unqualified(m_semaTypes.unref(x->semaType)) : 0;
    to = m_semaTypes.unqualified(m_semaTypes.unref(to));
    if (!xt || !xt->isArithmetic() || !to->isIntegral()) return false;
    if (xt->isFloating()) {
        double d = 0, raw = 0;
        if (!foldArith(x, d, depth + 1)) return false;
        if (to->kind == sema::Type::Bool) { bits = d != 0 ? 1 : 0; return d == d; }
        return foldFloatToInteger(d, sizeOf(to) * 8, to->isUnsigned(), bits, raw);
    }
    unsigned long long v = 0;
    if (x->semaConst) {
        // semaValue holds the value's 32 bits, sign-extended
        v = static_cast<unsigned long long>(static_cast<long long>(x->semaValue));
        if (sizeOf(xt) <= 4 && xt->isUnsigned()) v &= 0xFFFFFFFFULL;
    } else if (x->kind == cxx::Expr::FunctionalCast && x->kids.empty()) v = 0;   // "int()"
    else if ((x->kind == cxx::Expr::Cast || (x->kind == cxx::Expr::NamedCast && x->text == "static_cast") || x->kind == cxx::Expr::FunctionalCast) && x->kids.size() == 1 && !x->semaSym) {
        if (!foldInteger(x->kids[0], xt, v, depth + 1)) return false;
    } else return false;
    if (to->kind == sema::Type::Bool) { bits = v != 0 ? 1 : 0; return true; }
    const int n = sizeOf(to) * 8;
    if (n <= 0 || n > 64) return false;
    if (n < 64) {
        const unsigned long long mask = (1ULL << n) - 1;
        v &= mask;
        if (!to->isUnsigned() && (v >> (n - 1)) != 0) v |= ~mask;
    }
    bits = v;
    return true;
}

// v converted to the arithmetic type t as cl converts a constant: to an integer modulo 2^N; false when the result
// is not a plain value (NaN, an infinity converted to an integer, an enum)
bool IrGen::convertFolded(const sema::Type* t, double& v) const
{
    if (v != v) return false;
    if (t->kind == sema::Type::Float) { v = static_cast<double>(static_cast<float>(v)); return true; }   // to infinity beyond
    if (t->kind == sema::Type::Double || t->kind == sema::Type::LongDouble) return true;
    if (t->kind == sema::Type::Bool) { v = v != 0 ? 1 : 0; return true; }
    if (!t->isIntegral() || t->kind == sema::Type::Enum) return false;
    unsigned long long bits = 0;
    double raw = 0;
    if (!foldFloatToInteger(v, sizeOf(t) * 8, t->isUnsigned(), bits, raw)) return false;
    v = t->isUnsigned() ? static_cast<double>(bits) : static_cast<double>(static_cast<long long>(bits));
    return true;
}

// a floating expression value() folds to one constant when its operands are constants
bool IrGen::foldableFloating(cxx::Expr* e) const
{
    const sema::Type* t = e->semaType ? m_semaTypes.unref(e->semaType) : 0;
    if (!t || !t->isFloating()) return false;
    if (e->kind == cxx::Expr::Name) return true;
    return !e->semaSym && (e->kind == cxx::Expr::Binary || e->kind == cxx::Expr::Unary || e->kind == cxx::Expr::Cast || e->kind == cxx::Expr::NamedCast
                           || e->kind == cxx::Expr::FunctionalCast || e->kind == cxx::Expr::Cond);
}

// an array, or a plain aggregate class (no constructor, destructor, base, virtual function, union or bit-field;
// the layout ignores bit-fields, so such a class keeps being built by code): the elements in order, the padding
// and the members without an initializer zero
bool IrGen::aggregateData(const sema::Type* t, const std::vector<cxx::Expr*>* elems, std::vector<IrDataItem>& out, int depth)
{
    const sema::Type* u = m_semaTypes.unqualified(m_semaTypes.unref(t));
    const size_t given = elems ? elems->size() : 0;
    if (u->kind == sema::Type::Array) {
        if (u->arraySize <= 0 || given > static_cast<size_t>(u->arraySize)) return false;
        for (long i = 0; i < u->arraySize; ++i)
            if (!constantData(u->inner, static_cast<size_t>(i) < given ? (*elems)[static_cast<size_t>(i)] : 0, out, depth + 1)) return false;
        return true;
    }
    Symbol* c = u->kind == sema::Type::Class ? u->decl : 0;
    if (!c || !c->layoutDone || c->isUnion || c->isPolymorphic || !c->bases.empty() || needsDefaultCtor(u) || needsDtor(u)) return false;
    if (given > c->fields.size()) return false;
    int at = 0;
    for (size_t f = 0; f < c->fields.size(); ++f) {
        Symbol* fld = c->fields[f];
        if (isReference(fld->type) || fld->offset < at) return false;
        if (fld->declAst)
            for (size_t k = 0; k < fld->declAst->declarators.size(); ++k)
                if (fld->declAst->declarators[k].decl && fld->declAst->declarators[k].decl->semaSym == fld && fld->declAst->declarators[k].decl->bitfield) return false;
        zeroData(out, fld->offset - at);
        if (!constantData(fld->type, f < given ? (*elems)[f] : 0, out, depth + 1)) return false;
        at = fld->offset + sizeOf(fld->type);
    }
    if (at > sizeOf(u)) return false;
    zeroData(out, sizeOf(u) - at);
    return true;
}

// const all the way down (the elements of an array) and no mutable member: the object can live in .rdata
bool IrGen::readOnlyObject(const sema::Type* t) const
{
    const sema::Type* e = m_semaTypes.unref(t);
    while (e && e->kind == sema::Type::Array) { if (e->isConst) return true; e = e->inner; }
    if (!e || !e->isConst) return false;
    if (e->kind == sema::Type::Class && e->decl)
        for (size_t f = 0; f < e->decl->fields.size(); ++f) if (e->decl->fields[f]->isMutable) return false;
    return true;
}

// as cl does: once an object with a destructor is built, an internal thunk that destroys it is registered with
// atexit, so the objects are destroyed after main in the reverse order of their construction
void IrGen::registerAtExit(const std::string& global, const sema::Type* t, int line)
{
    ExitThunk x;
    // a C++ name ("cppparse_atexit_0()"), so that its address and its definition get the same symbol; internal:
    // a static local of an inline function registers the thunk of the unit that built it
    x.fn = m_prog.newSymbol(Symbol::Function, "cppparse_atexit_" + numText(static_cast<long>(m_exitThunks.size())), m_prog.global);
    x.fn->type = m_semaTypes.function(m_semaTypes.basic(sema::Type::Void), std::vector<const sema::Type*>(), false, false);
    x.fn->isStatic = true;
    x.fn->hasBody = true;
    x.fn->loc.line = line;
    x.global = global;
    x.type = t;
    m_exitThunks.push_back(x);
    IrInst fa = make(IrInst::AddrGlobal, 0, line);
    fa.type = m_types.function(oldType(m_semaTypes.basic(sema::Type::Void)));
    fa.args.push_back(IrOperand::global(irName(x.fn), fa.type));
    IrInst in = make(IrInst::Call, m_semaTypes.basic(sema::Type::Int), line);
    in.sub = "atexit";
    in.args.push_back(IrOperand::temp(emit(fa), fa.type));
    emit(in);
}

void IrGen::genExitThunks()
{
    for (; m_exitThunksDone < m_exitThunks.size(); ++m_exitThunksDone) {
        const ExitThunk x = m_exitThunks[m_exitThunksDone];
        const int line = x.fn->loc.line;
        m_out = new IrFunction;
        m_fn = x.fn;
        m_slots.clear(); m_loops.clear(); m_tryStack.clear(); m_tryCount = 0;
        prologue(x.fn);
        LValue lv; lv.kind = LValue::Global; lv.global = x.global; lv.type = x.type;
        destroy(x.type, addressOf(lv, line), line, false);
        returnSequence(line);
        emit(make(IrInst::Ret, 0, line));
        finishFunction(line);
    }
}

} // namespace cppparse
