// Sema.cpp - version 6 stage 3: types, symbols, declarations, lookup, class layout, templates.
// Expressions and statements are in SemaExpr.cpp.
#include "Sema.hpp"
#include "PathUtil.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace cppparse {
namespace sema {


bool Type::isDependent() const
{
    if (kind == Dependent) return true;
    if (inner && inner->isDependent()) return true;
    for (size_t i = 0; i < params.size(); ++i) if (params[i]->isDependent()) return true;
    return false;
}

std::string Type::key() const
{
    std::ostringstream s;
    s << (isConst ? "c" : "") << (isVolatile ? "v" : "") << static_cast<int>(kind);
    switch (kind) {
    case Pointer: case Reference: s << "(" << inner->key() << ")"; break;
    case Array: s << "[" << arraySize << "](" << inner->key() << ")"; break;
    case Function:
        s << "(" << inner->key() << ")(";
        for (size_t i = 0; i < params.size(); ++i) s << (i ? "," : "") << params[i]->key();
        s << (variadic ? "..." : "") << ")" << (constMethod ? "c" : "");
        break;
    case Class: case Enum: case Dependent: s << "@" << static_cast<const void*>(decl); break;
    case MemberPointer: s << "(" << memberClass->key() << ")::*(" << inner->key() << ")"; break;
    default: break;
    }
    return s.str();
}

std::string Type::text() const
{
    std::string cv = std::string(isConst ? "const " : "") + (isVolatile ? "volatile " : "");
    switch (kind) {
    case Void: return cv + "void";
    case Bool: return cv + "bool";
    case Char: return cv + "char";
    case SChar: return cv + "signed char";
    case UChar: return cv + "unsigned char";
    case WChar: return cv + "wchar_t";
    case Short: return cv + "short";
    case UShort: return cv + "unsigned short";
    case Int: return cv + "int";
    case UInt: return cv + "unsigned int";
    case Long: return cv + "long";
    case ULong: return cv + "unsigned long";
    case LongLong: return cv + "long long";
    case ULongLong: return cv + "unsigned long long";
    case Float: return cv + "float";
    case Double: return cv + "double";
    case LongDouble: return cv + "long double";
    case NullPtr: return "(null)";
    case Pointer: return inner->text() + "*" + (isConst ? " const" : "");
    case Reference: return inner->text() + "&";
    case Array: { std::ostringstream s; s << inner->text() << "[" << (arraySize >= 0 ? arraySize : 0) << "]"; return s.str(); }
    case Function: {
        std::string s = inner->text() + "(";
        for (size_t i = 0; i < params.size(); ++i) s += (i ? ", " : "") + params[i]->text();
        if (variadic) s += params.empty() ? "..." : ", ...";
        return s + ")" + (constMethod ? " const" : "");
    }
    case Class: case Enum: return cv + (decl ? decl->qualifiedName() : std::string("?"));
    case MemberPointer: return inner->text() + " " + memberClass->text() + "::*";
    case Dependent: return cv + (decl ? decl->name : std::string("T"));
    case Error: return "<error>";
    }
    return "?";
}

TypeTable::TypeTable()
{
    for (int k = 0; k <= Type::Error; ++k) {
        Type t;
        t.kind = static_cast<Type::Kind>(k);
        m_basic[k] = (k >= Type::Pointer && k <= Type::Dependent) ? 0 : intern(t);
    }
    setTarget(defaultTarget());
}

void TypeTable::setTarget(const Target& target)
{
    m_pointerSize = target.pointerSize;
    m_longSize = target.longSize;
    m_wcharSize = target.wcharSize;
    m_longDoubleSize = target.longDoubleSize;
    m_maxAlign = target.maxScalarAlign;
    m_sizeIsLong = target.sizeTypeIsLong;
}

TypeTable::~TypeTable()
{
    for (std::map<std::string, Type*>::iterator it = m_types.begin(); it != m_types.end(); ++it) delete it->second;
}

const Type* TypeTable::intern(const Type& t)
{
    std::string k = t.key();
    std::map<std::string, Type*>::iterator it = m_types.find(k);
    if (it != m_types.end()) return it->second;
    Type* n = new Type(t);
    m_types[k] = n;
    return n;
}

const Type* TypeTable::qualified(const Type* t, bool c, bool v)
{
    if (!t || (!c && !v) || (t->isConst || !c) && (t->isVolatile || !v)) return t;
    if (t->kind == Type::Reference || t->kind == Type::Function) return t;    // references and functions carry no cv
    if (t->kind == Type::Array) return array(qualified(t->inner, c, v), t->arraySize);
    Type n = *t;
    n.isConst = n.isConst || c;
    n.isVolatile = n.isVolatile || v;
    return intern(n);
}

const Type* TypeTable::unqualified(const Type* t)
{
    if (!t || (!t->isConst && !t->isVolatile)) return t;
    Type n = *t;
    n.isConst = false; n.isVolatile = false;
    return intern(n);
}

const Type* TypeTable::pointer(const Type* to) { Type t; t.kind = Type::Pointer; t.inner = to; return intern(t); }
const Type* TypeTable::reference(const Type* to) { if (to && to->kind == Type::Reference) return to; Type t; t.kind = Type::Reference; t.inner = to; return intern(t); }
const Type* TypeTable::array(const Type* of, long size) { Type t; t.kind = Type::Array; t.inner = of; t.arraySize = size; return intern(t); }
const Type* TypeTable::memberPointer(const Type* cls, const Type* target) { Type t; t.kind = Type::MemberPointer; t.memberClass = cls; t.inner = target; return intern(t); }
const Type* TypeTable::classType(Symbol* cls) { Type t; t.kind = Type::Class; t.decl = cls; return intern(t); }
const Type* TypeTable::enumType(Symbol* en) { Type t; t.kind = Type::Enum; t.decl = en; return intern(t); }
const Type* TypeTable::dependent(Symbol* param) { Type t; t.kind = Type::Dependent; t.decl = param; return intern(t); }

const Type* TypeTable::function(const Type* ret, const std::vector<const Type*>& params, bool variadic, bool constMethod)
{
    Type t; t.kind = Type::Function; t.inner = ret; t.params = params; t.variadic = variadic; t.constMethod = constMethod;
    return intern(t);
}

const Type* TypeTable::decay(const Type* t)
{
    if (!t) return t;
    if (t->kind == Type::Reference) t = t->inner;
    if (t->kind == Type::Array) return pointer(t->inner);
    if (t->kind == Type::Function) return pointer(t);
    return t;
}

// cl's inheritance model of a class for its member pointers: multiple when it or a base has several direct
// bases, virtual when it has a virtual base anywhere
void TypeTable::memberPointerModel(const Symbol* cls, bool& multiple, bool& virt, int depth)
{
    if (!cls || depth > 64) return;
    if (cls->bases.size() > 1) multiple = true;
    // a vfptr of its own in front of a base without one: the base is not at offset 0 (cl: multiple)
    for (size_t b = 0; b < cls->bases.size(); ++b) if (cls->isPolymorphic && !cls->bases[b].isVirtual && cls->bases[b].cls && !cls->bases[b].cls->isPolymorphic) multiple = true;
    for (size_t b = 0; b < cls->bases.size(); ++b) {
        if (cls->bases[b].isVirtual) virt = true;
        memberPointerModel(cls->bases[b].cls, multiple, virt, depth + 1);
    }
}

bool TypeTable::holdsMemberPointer(const Type* t, int depth)
{
    while (t && (t->kind == Type::Array || t->kind == Type::Reference)) { if (t->kind == Type::Reference) return false; t = t->inner; }
    if (!t || depth > 32) return false;
    if (t->kind == Type::MemberPointer) return true;
    if (t->kind != Type::Class || !t->decl) return false;
    for (size_t b = 0; b < t->decl->bases.size(); ++b) if (t->decl->bases[b].cls && holdsMemberPointer(t->decl->bases[b].cls->type, depth + 1)) return true;
    for (size_t f = 0; f < t->decl->fields.size(); ++f) if (holdsMemberPointer(t->decl->fields[f]->type, depth + 1)) return true;
    return false;
}

int TypeTable::sizeOf(const Type* t) const
{
    if (!t) return 8;
    switch (t->kind) {
    case Type::Void: return 1;
    case Type::Bool: case Type::Char: case Type::SChar: case Type::UChar: return 1;
    case Type::Short: case Type::UShort: return 2;
    case Type::WChar: return m_wcharSize;
    case Type::Int: case Type::UInt: case Type::Float: case Type::Enum: return 4;
    case Type::Long: case Type::ULong: return m_longSize;
    case Type::LongLong: case Type::ULongLong: case Type::Double: return 8;
    case Type::LongDouble: return m_longDoubleSize;
    case Type::Pointer: case Type::Reference: case Type::NullPtr: return m_pointerSize;
    case Type::MemberPointer: {
        // cl's representations by the class's inheritance model: data, a 4-byte offset (plus a vbtable index
        // with virtual bases); functions, a code pointer (plus a this adjustment with multiple inheritance, plus
        // a vbtable index with virtual bases), rounded to the pointer's alignment
        bool multiple = false, virt = false;
        Symbol* mc = t->memberClass ? t->memberClass->decl : 0;
        // cl fixes the model of a class the first time it needs the size of one of its member pointers (sizeof, a
        // member's layout): while the class is incomplete that is the unspecified model (data 12, functions a
        // pointer plus three ints), which it keeps after the class is defined; naming the type alone (a typedef, a
        // parameter) decides nothing
        if (mc && !mc->isDefinition) mc->memberPointerUnspecified = true;
        if (!mc || mc->memberPointerUnspecified) return !t->inner || t->inner->kind != Type::Function ? 12 : (m_pointerSize + 12 + m_pointerSize - 1) / m_pointerSize * m_pointerSize;
        memberPointerModel(mc, multiple, virt, 0);
        if (!t->inner || t->inner->kind != Type::Function) return virt ? 8 : 4;
        const int n = m_pointerSize + (multiple || virt ? 4 : 0) + (virt ? 4 : 0);
        return (n + m_pointerSize - 1) / m_pointerSize * m_pointerSize;
    }
    case Type::Array: return t->arraySize > 0 ? static_cast<int>(t->arraySize) * sizeOf(t->inner) : 0;
    case Type::Class: return t->decl && t->decl->layoutDone ? t->decl->size : 8;
    default: return 8;
    }
}

int TypeTable::alignOf(const Type* t) const
{
    if (!t) return 8;
    if (t->kind == Type::Array) return alignOf(t->inner);
    if (t->kind == Type::Class) return t->decl && t->decl->layoutDone ? t->decl->align : 8;
    if (t->kind == Type::MemberPointer) return t->inner && t->inner->kind == Type::Function ? m_pointerSize : 4;
    int s = sizeOf(t);
    return s > m_maxAlign ? m_maxAlign : (s < 1 ? 1 : s);
}

int TypeTable::rank(const Type* t) const
{
    switch (t->kind) {
    case Type::Bool: return 1;
    case Type::Char: case Type::SChar: case Type::UChar: return 2;
    case Type::WChar: case Type::Short: case Type::UShort: return 3;
    case Type::Int: case Type::UInt: case Type::Enum: return 4;
    case Type::Long: case Type::ULong: return 5;
    case Type::LongLong: case Type::ULongLong: return 6;
    default: return 0;
    }
}

const Type* TypeTable::promoted(const Type* t)
{
    t = unqualified(unref(t));
    if (t->kind == Type::Float) return basic(Type::Double);
    if (t->isIntegral() && rank(t) < 4) return basic(Type::Int);
    if (t->kind == Type::Enum) return basic(Type::Int);
    return t;
}

const Type* TypeTable::arithmeticResult(const Type* a, const Type* b)
{
    // the integral promotions only: float op float stays float (C++98 5/9), computed in single precision
    a = unqualified(unref(a)); b = unqualified(unref(b));
    if (a->kind != Type::Float) a = promoted(a);
    if (b->kind != Type::Float) b = promoted(b);
    if (a->kind == Type::LongDouble || b->kind == Type::LongDouble) return basic(Type::LongDouble);
    if (a->kind == Type::Double || b->kind == Type::Double) return basic(Type::Double);
    if (a->kind == Type::Float || b->kind == Type::Float) return basic(Type::Float);
    if (rank(a) == rank(b)) return a->isUnsigned() ? a : b;
    const Type* hi = rank(a) > rank(b) ? a : b;
    const Type* lo = hi == a ? b : a;
    // long and unsigned int have the same size here: the signed type cannot hold every value of the other
    if (!hi->isUnsigned() && lo->isUnsigned() && sizeOf(hi) == sizeOf(lo))
        return basic(hi->kind == Type::Long ? Type::ULong : hi->kind == Type::LongLong ? Type::ULongLong : Type::UInt);
    return hi;
}


Symbol::Symbol()
    : kind(Variable), scope(0), type(0), access(cxx::AccessPublic), members(0), isDefinition(false), isStruct(false), isUnion(false),
      isPolymorphic(false), isAbstract(false), layoutDone(false), size(0), align(1), nvSize(0),
      leadsWithZeroSized(false), endsWithZeroSized(false), vptrOffset(-1), vbptrOffset(-1), classAst(0),
      isVirtual(false), isPureVirtual(false), isStatic(false), isExtern(false), isInline(false), isExplicit(false), isConstructor(false),
      isDestructor(false), isConversion(false), isOperator(false), isFriendDecl(false), hasBody(false), isExternC(false),
      definition(0), declAst(0), body(0), vtableSlot(-1), thisAdjust(0), vfptrOffset(0), vfptrVbase(0), overrides(0), ownerClass(0), offset(0), index(0), isMember(false), isParam(false),
      isLocal(false), isMutable(false), init(0), initCtor(0), hasConstValue(false), constValue(0), declAlign(0), bitWidth(-1), bitOffset(0),
      anonHost(0), anonOf(0), templateAst(0), instantiatedFrom(0),
      isDependentContext(false), isImplicitInstantiation(false), isLazyInstantiation(false), instantiationFailed(false), isImplicit(false), requiredParams(0), hasUserCtor(false), hasCopyCtor(false), hasDtor(false),
      hasCopyAssign(false), hasDefaultCtor(false), memberPointerUnspecified(false), defOrder(0), friendsUnknown(false), isUsed(false), isNoreturn(false) {}

std::string Symbol::qualifiedName() const
{
    std::string s = name;
    for (Scope* sc = scope; sc; sc = sc->parent) {
        if ((sc->kind == Scope::Namespace || sc->kind == Scope::Class) && sc->owner && !sc->owner->name.empty()) s = sc->owner->name + "::" + s;
        if (sc->kind == Scope::Function || sc->kind == Scope::Block) break;
    }
    return s;
}

Program::Program()
{
    global = newScope(Scope::Global, 0, 0);
}

Program::~Program()
{
    for (size_t i = 0; i < symbols.size(); ++i) delete symbols[i];
    for (size_t i = 0; i < scopes.size(); ++i) delete scopes[i];
}

Symbol* Program::newSymbol(Symbol::Kind k, const std::string& name, Scope* in)
{
    Symbol* s = new Symbol;
    s->kind = k;
    s->name = name;
    s->scope = in;
    symbols.push_back(s);
    if (in) in->add(s);
    return s;
}

Scope* Program::newScope(Scope::Kind k, Scope* parent, Symbol* owner)
{
    Scope* s = new Scope;
    s->kind = k;
    s->parent = parent;
    s->owner = owner;
    scopes.push_back(s);
    return s;
}


Sema::Sema(TypeTable& types, Program& program)
    : m_types(types), m_prog(program), m_currentFunction(0), m_currentClass(0), m_loopDepth(0), m_switchDepth(0), m_depth(0),
      m_classOrder(0), m_bodyOrder(-1), m_definingDepth(0), m_hasGoto(false), m_strictDeduce(false), m_instantiationDepth(0), m_instantiationOverflow(false), m_externalArena(0) {}

void Sema::queueBody(PendingBody pb)
{
    // a body queued while bodies are analysed (an instantiation's member) is compiled after the whole unit, as cl
    // instantiates them; a member function defined in its class gets the order where the outermost class ends
    pb.order = m_bodyOrder >= 0 ? 0x7FFFFFFF : m_classOrder;
    m_pending.push_back(pb);
}

// sizeof of a pointer to member of a class defined after the body (or initializer) being analysed: cl met the
// class incomplete there and keeps its unspecified model (TypeTable::sizeOf)
void Sema::memberPointerSizeUse(const Type* t)
{
    while (t && t->kind == Type::Array) t = t->inner;
    if (!t || t->kind != Type::MemberPointer || !t->memberClass || !t->memberClass->decl) return;
    Symbol* cls = t->memberClass->decl;
    if (m_bodyOrder >= 0 && cls->defOrder > m_bodyOrder) cls->memberPointerUnspecified = true;
}

Sema::~Sema()
{
    for (size_t i = 0; i < m_ownedDecls.size(); ++i) delete m_ownedDecls[i];
    for (size_t i = 0; i < m_ownedDeclarators.size(); ++i) delete m_ownedDeclarators[i];
}

std::string Sema::where(const cxx::Loc& loc) const
{
    std::ostringstream s;
    s << (loc.file >= 0 && loc.file < static_cast<int>(m_files.size()) ? m_files[static_cast<size_t>(loc.file)] : std::string("?")) << "(" << loc.line << ")";
    return s.str();
}

void Sema::error(const cxx::Loc& loc, const char* code, const std::string& msg)
{
    Diag d; d.severity = Diag::Error; d.code = code; d.loc = loc; d.message = msg;
    m_diags.push_back(d);
}

void Sema::warning(const cxx::Loc& loc, const char* code, const std::string& msg)
{
    Diag d; d.severity = Diag::Warning; d.code = code; d.loc = loc; d.message = msg;
    m_diags.push_back(d);
}

int Sema::errorCount() const
{
    int n = 0;
    for (size_t i = 0; i < m_diags.size(); ++i) if (m_diags[i].severity == Diag::Error) ++n;
    return n;
}


bool Sema::lookupMember(Symbol* cls, const std::string& name, std::vector<Symbol*>& out, int depth)
{
    if (!cls || !cls->members || depth > 32) return false;
    const std::vector<Symbol*>* v = cls->members->find(name);
    if (v) { out.insert(out.end(), v->begin(), v->end()); return true; }
    bool found = false;
    const size_t before = out.size();
    for (size_t b = 0; b < cls->bases.size(); ++b) if (lookupMember(cls->bases[b].cls, name, out, depth + 1)) found = true;
    if (found && !cls->vbases.empty() && out.size() > before + 1) {
        // 10.2: a declaration found in a virtual base is hidden by one found in a class derived from it on another
        // path (VL::f hides V::f in VD : VL, VR with V virtual); the same symbol found twice is one declaration
        std::vector<Symbol*> kept;
        for (size_t i = before; i < out.size(); ++i) {
            Symbol* s = out[i];
            Symbol* owner = s->ownerClass ? s->ownerClass : (s->scope && s->scope->kind == Scope::Class ? s->scope->owner : 0);
            bool hidden = false;
            if (owner && std::find(cls->vbases.begin(), cls->vbases.end(), owner) != cls->vbases.end())
                for (size_t k = before; k < out.size() && !hidden; ++k) {
                    Symbol* o = out[k]->ownerClass ? out[k]->ownerClass : (out[k]->scope && out[k]->scope->kind == Scope::Class ? out[k]->scope->owner : 0);
                    if (o && o != owner && derivesFrom(o, owner, 0)) hidden = true;
                }
            if (!hidden && std::find(kept.begin(), kept.end(), s) == kept.end()) kept.push_back(s);
        }
        out.resize(before);
        out.insert(out.end(), kept.begin(), kept.end());
    }
    return found;
}

bool Sema::lookupInScope(Scope* s, const std::string& name, bool typesOnly, std::vector<Symbol*>& out, std::set<Scope*>& seen)
{
    if (!s || seen.count(s)) return false;
    seen.insert(s);
    bool found = false;
    if (s->kind == Scope::Class && s->owner) {
        std::vector<Symbol*> tmp;
        if (lookupMember(s->owner, name, tmp)) {
            for (size_t i = 0; i < tmp.size(); ++i) if (!typesOnly || tmp[i]->isType() || tmp[i]->kind == Symbol::ClassTemplate) out.push_back(tmp[i]);
            found = !out.empty();
        }
    } else {
        const std::vector<Symbol*>* v = s->find(name);
        // a function declared only as a friend inside a class is not found by ordinary lookup (7.3.1.2/3), only by
        // argument-dependent lookup, until the namespace declares it
        if (v) for (size_t i = 0; i < v->size(); ++i) if (!(*v)[i]->isFriendDecl && (!typesOnly || (*v)[i]->isType() || (*v)[i]->kind == Symbol::ClassTemplate)) { out.push_back((*v)[i]); found = true; }
    }
    // namespaces nominated by using-directives in this scope
    for (size_t u = 0; u < s->usingDirectives.size(); ++u) if (lookupInScope(s->usingDirectives[u], name, typesOnly, out, seen)) found = true;
    return found;
}

// 3.4.2/2: the namespaces associated with a class are the innermost enclosing namespaces of the class, of its
// direct and indirect bases and of the classes its template arguments name; of an enumeration its namespace; of a
// pointer, reference or array those of the pointed-to or element type; of a function type those of its parameter
// and return types
void Sema::associatedNamespaces(const Type* t, std::vector<Scope*>& out, int depth)
{
    if (!t || depth > 16) return;
    switch (t->kind) {
    case Type::Pointer: case Type::Reference: case Type::Array: associatedNamespaces(t->inner, out, depth + 1); return;
    case Type::Function: associatedNamespaces(t->inner, out, depth + 1); for (size_t i = 0; i < t->params.size(); ++i) associatedNamespaces(t->params[i], out, depth + 1); return;
    case Type::Class: case Type::Enum: break;
    default: return;
    }
    Symbol* d = t->decl;
    if (!d) return;
    Scope* ns = d->scope;
    while (ns && !ns->isNamespaceLike()) ns = ns->parent;
    if (ns && std::find(out.begin(), out.end(), ns) == out.end()) out.push_back(ns);
    if (t->kind != Type::Class) return;
    for (size_t b = 0; b < d->bases.size(); ++b) if (d->bases[b].cls) associatedNamespaces(d->bases[b].cls->type, out, depth + 1);
    for (size_t a = 0; a < d->templateArgs.size(); ++a) if (d->templateArgs[a]) associatedNamespaces(d->templateArgs[a], out, depth + 1);
}

void Sema::adlCandidates(const std::string& name, const std::vector<cxx::Expr*>& args, size_t firstArg, std::vector<Symbol*>& cands)
{
    std::vector<Scope*> nss;
    for (size_t a = firstArg; a < args.size(); ++a) if (args[a] && args[a]->semaType) associatedNamespaces(m_types.unref(args[a]->semaType), nss, 0);
    for (size_t n = 0; n < nss.size(); ++n) {
        const std::vector<Symbol*>* v = nss[n]->find(name);
        if (!v) continue;
        for (size_t k = 0; k < v->size(); ++k) {
            Symbol* f = (*v)[k];
            if (f->kind != Symbol::Function && f->kind != Symbol::FunctionTemplate) continue;
            if (std::find(cands.begin(), cands.end(), f) == cands.end()) cands.push_back(f);
        }
    }
}

Symbol* Sema::lookupUnqualified(const std::string& name, Scope* from, bool typesOnly, std::vector<Symbol*>* all)
{
    for (Scope* s = from; s; s = s->parent) {
        std::vector<Symbol*> out;
        std::set<Scope*> seen;
        if (lookupInScope(s, name, typesOnly, out, seen) && !out.empty()) {
            if (all) *all = out;
            return out[0];
        }
        // the enclosing class of an out-of-line member function: its scope is the class's member scope (parent chain)
    }
    return 0;
}

// inside an instantiation the template's name is the injected class name, a typedef of the instantiation; written
// with template arguments ("Matrix<T, C, R>" inside Matrix<T, R, C>) it names the template, so those arguments
// select the specialisation (14.6.1/1). Returns the class template then, 0 otherwise
Symbol* Sema::injectedTemplate(Symbol* s, const cxx::NameComponent& c) const
{
    if (!s || !c.hasTemplateArgs || s->kind != Symbol::Typedef || !s->type || s->type->kind != Type::Class || !s->type->decl) return 0;
    Symbol* tmpl = s->type->decl->instantiatedFrom;
    return tmpl && tmpl->kind == Symbol::ClassTemplate && tmpl->name == s->name ? tmpl : 0;
}

Scope* Sema::scopeForQualifier(const cxx::QualifiedName& n, Scope* from)
{
    // the scope denoted by all components but the last
    if (n.parts.size() <= 1) return n.global ? m_prog.global : 0;
    Scope* cur = n.global ? m_prog.global : 0;
    for (size_t i = 0; i + 1 < n.parts.size(); ++i) {
        const cxx::NameComponent& c = n.parts[i];
        Symbol* s = 0;
        if (cur) {
            std::vector<Symbol*> out;
            if (cur->kind == Scope::Class) lookupMember(cur->owner, c.ident, out);
            else { const std::vector<Symbol*>* v = cur->find(c.ident); if (v) out = *v; std::set<Scope*> seen; if (out.empty()) lookupInScope(cur, c.ident, true, out, seen); }
            for (size_t k = 0; k < out.size() && !s; ++k) if (out[k]->kind == Symbol::Namespace || out[k]->kind == Symbol::Class || out[k]->kind == Symbol::ClassTemplate || out[k]->kind == Symbol::Typedef) s = out[k];
        } else {
            // scope by scope: inside class X the name X first means its constructors, which do not stop the search
            for (Scope* sc = from; sc && !s; sc = sc->parent) {
                std::vector<Symbol*> out;
                std::set<Scope*> seen;
                lookupInScope(sc, c.ident, false, out, seen);
                for (size_t k = 0; k < out.size() && !s; ++k) if (out[k]->kind == Symbol::Namespace || out[k]->kind == Symbol::Class || out[k]->kind == Symbol::ClassTemplate || out[k]->kind == Symbol::Typedef) s = out[k];
            }
        }
        if (!s) return 0;
        if (Symbol* tmpl = injectedTemplate(s, c)) s = tmpl;    // "M<T, C, R>::" inside M<T, R, C>: another instantiation
        if (s->kind == Symbol::Typedef) { const Type* t = s->type; if (t && t->kind == Type::Class && t->decl) s = t->decl; else return 0; }
        if (s->kind == Symbol::ClassTemplate && !c.hasTemplateArgs) {
            // the template's name inside one of its instantiations is the injected class name
            for (Scope* sc = from; sc && s->kind == Symbol::ClassTemplate; sc = sc->parent) {
                if (sc->kind != Scope::Instantiation) continue;
                const std::vector<Symbol*>* v = sc->find(s->name);
                if (v) for (size_t k = 0; k < v->size(); ++k) if ((*v)[k]->kind == Symbol::Typedef && (*v)[k]->type && (*v)[k]->type->kind == Type::Class && (*v)[k]->type->decl) { s = (*v)[k]->type->decl; break; }
            }
        }
        if (s->kind == Symbol::ClassTemplate) {
            if (!c.hasTemplateArgs) return 0;
            const Type* inst = instantiateClassTemplate(s, c.templateArgs, from, cxx::Loc());
            if (!inst || inst->kind != Type::Class) return 0;
            s = inst->decl;
        }
        cur = s->members;
        if (!cur) return 0;
    }
    return cur;
}

Symbol* Sema::resolveName(const cxx::QualifiedName& n, Scope* from, bool typesOnly, std::vector<Symbol*>* overloads)
{
    if (n.parts.empty()) return 0;
    const std::string& last = n.parts.back().ident;
    if (n.parts.size() == 1 && !n.global) {
        std::vector<Symbol*> all;
        Symbol* s = lookupUnqualified(last, from, typesOnly, &all);
        if (overloads) *overloads = all;
        return s;
    }
    Scope* sc = scopeForQualifier(n, from);
    if (!sc) return 0;
    std::vector<Symbol*> out;
    if (sc->kind == Scope::Class) lookupMember(sc->owner, last, out);
    else { std::set<Scope*> seen; lookupInScope(sc, last, typesOnly, out, seen); }
    if (typesOnly) { std::vector<Symbol*> t; for (size_t i = 0; i < out.size(); ++i) if (out[i]->isType() || out[i]->kind == Symbol::ClassTemplate) t.push_back(out[i]); out = t; }
    if (overloads) *overloads = out;
    return out.empty() ? 0 : out[0];
}

Symbol* Sema::lookupQualified(const cxx::QualifiedName& n, Scope* from, bool typesOnly) { return resolveName(n, from, typesOnly, 0); }


bool Sema::templateArgText(const std::vector<cxx::TemplateArg>& args, Scope* from, std::vector<const Type*>& types, std::vector<long>& ints, std::string& text)
{
    text = "<";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) text += ", ";
        const cxx::TemplateArg& a = args[i];
        if (a.type) {
            // a template's name alone (Box, the argument of a template template parameter, 14.3.3): a class type
            // whose declaration is the template (an alias of it binds the parameter in the instantiation)
            const cxx::DeclSpec& sp = a.type->spec;
            if (sp.kind == cxx::DeclSpec::Named && sp.name.parts.size() == 1 && !sp.name.global && !sp.name.parts[0].hasTemplateArgs && (!a.type->decl || a.type->decl->parts.empty())) {
                Symbol* ts = resolveName(sp.name, from, true, 0);
                if (ts && ts->kind == Symbol::ClassTemplate) {
                    while (ts->kind == Symbol::ClassTemplate && !ts->templateAst && ts->instantiatedFrom) ts = ts->instantiatedFrom;
                    const Type* t = m_types.classType(ts);
                    types.push_back(t); ints.push_back(0); text += ts->name;
                    continue;
                }
            }
            const Type* t = typeOf(a.type->spec, from);
            t = applyDeclarator(t, *a.type->decl, from, 0);
            if (!t || t->kind == Type::Error) return false;
            types.push_back(t); ints.push_back(0);
            text += t->text();
        } else if (a.expr) {
            const Type* et = expr(a.expr, from);
            if (et && et->kind == Type::Dependent) { types.push_back(et); ints.push_back(0); text += "?"; continue; }
            // a type named by an expression (an identifier that is a type)
            if (a.expr->kind == cxx::Expr::Name) {
                Symbol* ts = resolveName(a.expr->name, from, true, 0);
                if (ts && ts->isType()) { const Type* t = ts->kind == Symbol::Typedef ? ts->type : ts->kind == Symbol::Class ? m_types.classType(ts) : ts->kind == Symbol::Enum ? m_types.enumType(ts) : m_types.dependent(ts); types.push_back(t); ints.push_back(0); text += t->text(); continue; }
                // a class template (the argument of a template template parameter, 14.3.3): carried as a class type
                // whose declaration is the template (an alias of it binds the parameter in the instantiation)
                if (ts && ts->kind == Symbol::ClassTemplate && !a.expr->name.parts.back().hasTemplateArgs) {
                    while (ts->kind == Symbol::ClassTemplate && !ts->templateAst && ts->instantiatedFrom) ts = ts->instantiatedFrom;
                    const Type* t = m_types.classType(ts);
                    types.push_back(t); ints.push_back(0); text += ts->name;
                    continue;
                }
            }
            long v;
            if (!constantValue(a.expr, v)) return false;
            types.push_back(0); ints.push_back(v);
            std::ostringstream s; s << v; text += s.str();
        }
    }
    text += ">";
    return true;
}

const Type* Sema::namedType(const cxx::QualifiedName& n, Scope* in, const std::string& elaborated, const cxx::Loc& loc)
{
    std::vector<Symbol*> cands;
    Symbol* s = resolveName(n, in, true, &cands);
    const cxx::NameComponent& last = n.parts.back();
    if (!s && elaborated == "class" || !s && elaborated == "struct" || !s && elaborated == "union") {
        // "class X" of an undeclared X declares it in the enclosing namespace scope
        if (n.parts.size() == 1 && !n.global) {
            Scope* target = in; while (target && !target->isNamespaceLike() && target->kind != Scope::Class) target = target->parent;
            s = m_prog.newSymbol(Symbol::Class, last.ident, target ? target : m_prog.global);
            s->isStruct = elaborated != "class"; s->isUnion = elaborated == "union"; s->loc = loc;
            s->type = m_types.classType(s);
            return s->type;
        }
    }
    if (!s) {
        if (!m_reportedUnknown.count(n.text())) { m_reportedUnknown.insert(n.text()); error(loc, "S100", "unknown type '" + n.text() + "'"); }
        return m_types.error();
    }
    if (s->kind == Symbol::ClassTemplate) {
        while (s->kind == Symbol::ClassTemplate && !s->templateAst && s->instantiatedFrom) s = s->instantiatedFrom;   // a bound template template parameter
        if (!last.hasTemplateArgs) {
            // the injected class name inside the template itself, or a dependent use
            if (s->isDependentContext || in->kind == Scope::TemplateParams) return m_types.dependent(s);
            error(loc, "S111", "template '" + n.text() + "' needs arguments");
            return m_types.error();
        }
        return instantiateClassTemplate(s, last.templateArgs, in, loc);
    }
    if (Symbol* tmpl = injectedTemplate(s, last)) return instantiateClassTemplate(tmpl, last.templateArgs, in, loc);
    if (s->kind == Symbol::Typedef) return s->type ? s->type : m_types.error();
    if (s->kind == Symbol::Class) return m_types.classType(s);
    if (s->kind == Symbol::Enum) return m_types.enumType(s);
    if (s->kind == Symbol::TemplateParam) return s->type ? s->type : m_types.dependent(s);
    error(loc, "S100", "'" + n.text() + "' is not a type");
    return m_types.error();
}

const Type* Sema::typeOf(const cxx::DeclSpec& spec, Scope* in)
{
    const Type* t = 0;
    switch (spec.kind) {
    case cxx::DeclSpec::None: t = m_types.error(); break;
    case cxx::DeclSpec::Builtin: {
        Type::Kind k = Type::Int;
        if (spec.builtinVoid) k = Type::Void;
        else if (spec.builtinBool) k = Type::Bool;
        else if (spec.builtinWchar) k = Type::WChar;
        else if (spec.builtinFloat) k = Type::Float;
        else if (spec.builtinDouble) k = spec.builtinLong ? Type::LongDouble : Type::Double;
        else if (spec.builtinChar) k = spec.builtinUnsigned ? Type::UChar : spec.builtinSigned ? Type::SChar : Type::Char;
        else if (spec.builtinShort) k = spec.builtinUnsigned ? Type::UShort : Type::Short;
        else if (spec.builtinLong >= 2) k = spec.builtinUnsigned ? Type::ULongLong : Type::LongLong;
        else if (spec.builtinLong == 1) k = spec.builtinUnsigned ? Type::ULong : Type::Long;
        else k = spec.builtinUnsigned ? Type::UInt : Type::Int;
        t = m_types.basic(k);
        break;
    }
    case cxx::DeclSpec::Named: t = namedType(spec.name, in, spec.elaborated, spec.loc); break;
    case cxx::DeclSpec::Class: {
        // the class was declared when the enclosing declaration was processed: find it again by name
        if (spec.classDef && !spec.classDef->name.empty()) {
            Symbol* s = resolveName(spec.classDef->name, in, true, 0);
            if (s && s->kind == Symbol::Class) { t = m_types.classType(s); break; }
        }
        if (spec.classDef) { Symbol* s = declareClass(spec.classDef, in, cxx::AccessPublic, 0); t = s ? m_types.classType(s) : m_types.error(); }
        else t = m_types.error();
        break;
    }
    case cxx::DeclSpec::Enum: {
        if (spec.enumDef && !spec.enumDef->name.empty()) {
            cxx::QualifiedName q; cxx::NameComponent c; c.ident = spec.enumDef->name; q.parts.push_back(c);
            Symbol* s = resolveName(q, in, true, 0);
            if (s && s->kind == Symbol::Enum) { t = m_types.enumType(s); break; }
        }
        if (spec.enumDef) { Symbol* s = declareEnum(spec.enumDef, in, cxx::AccessPublic); t = s ? m_types.enumType(s) : m_types.error(); }
        else t = m_types.error();
        break;
    }
    }
    return m_types.qualified(t, spec.isConst, spec.isVolatile);
}

const Type* Sema::applyDeclarator(const Type* base, const cxx::Declarator& d, Scope* in, std::vector<Symbol*>* params)
{
    const Type* t = base;
    for (size_t i = 0; i < d.parts.size(); ++i) {
        const cxx::DeclaratorPart& p = d.parts[i];
        switch (p.kind) {
        case cxx::DeclaratorPart::Pointer: t = m_types.qualified(m_types.pointer(t), p.isConst, p.isVolatile); break;
        case cxx::DeclaratorPart::Reference: t = m_types.reference(t); break;
        case cxx::DeclaratorPart::Array: {
            long n = -1;
            if (p.arraySize) expr(p.arraySize, in);
            if (p.arraySize && !constantValue(p.arraySize, n)) { if (!(p.arraySize->semaType && p.arraySize->semaType->kind == Type::Dependent)) error(d.loc, "S114", "array bound is not a constant"); n = -1; }
            t = m_types.array(t, n);
            break;
        }
        case cxx::DeclaratorPart::MemberPointer: {
            const Type* cls = namedType(p.memberClass, in, "", d.loc);
            t = m_types.memberPointer(cls, t);
            break;
        }
        case cxx::DeclaratorPart::Function: {
            std::vector<const Type*> pts;
            for (size_t k = 0; k < p.params.size(); ++k) {
                const cxx::Param* pa = p.params[k];
                const Type* pt = typeOf(pa->spec, in);
                if (pa->decl) pt = applyDeclarator(pt, *pa->decl, in, 0);
                // parameters of array and function type decay
                if (pt->kind == Type::Array || pt->kind == Type::Function) pt = m_types.decay(pt);
                pts.push_back(pt);
                if (params) {
                    Symbol* ps = m_prog.newSymbol(Symbol::Variable, pa->decl ? pa->decl->name.text() : std::string(""), 0);
                    ps->type = pt; ps->isParam = true; ps->isLocal = true; ps->index = static_cast<int>(k); ps->loc = pa->loc; ps->init = pa->defaultValue;
                    params->push_back(ps);
                }
            }
            t = m_types.function(t, pts, p.variadic, p.isConst);
            break;
        }
        case cxx::DeclaratorPart::Paren: break;
        }
    }
    return t;
}


void Sema::analyze(cxx::TranslationUnit& tu, const std::vector<std::string>& files)
{
    m_files = files;
    declareAll(tu.decls, m_prog.global, cxx::AccessPublic);
    analyzeBodies();
    reportUsedFailedMembers();
}

void Sema::declareAll(const std::vector<cxx::Decl*>& decls, Scope* in, cxx::Access access)
{
    for (size_t i = 0; i < decls.size(); ++i) {
        cxx::Decl* d = decls[i];
        cxx::Access a = in->kind == Scope::Class ? d->access : access;
        declare(d, in, a);
    }
}

void Sema::declare(cxx::Decl* d, Scope* in, cxx::Access access)
{
    if (!d) return;
    switch (d->kind) {
    case cxx::Decl::Namespace: {
        Symbol* ns = 0;
        const std::vector<Symbol*>* v = in->find(d->name.empty() ? std::string("(anonymous)") : d->name);
        if (v) for (size_t i = 0; i < v->size() && !ns; ++i) if ((*v)[i]->kind == Symbol::Namespace) ns = (*v)[i];
        if (!ns) {
            ns = m_prog.newSymbol(Symbol::Namespace, d->name.empty() ? std::string("(anonymous)") : d->name, in);
            ns->loc = d->loc;
            ns->members = m_prog.newScope(Scope::Namespace, in, ns);
            if (d->name.empty()) { ns->name = ""; in->usingDirectives.push_back(ns->members); }
        }
        d->semaSym = ns;
        declareAll(d->decls, ns->members, cxx::AccessPublic);
        break;
    }
    case cxx::Decl::NamespaceAlias: {
        Symbol* target = resolveName(d->target, in, false, 0);
        if (!target || target->kind != Symbol::Namespace) { error(d->loc, "S115", "'" + d->target.text() + "' is not a namespace"); break; }
        Symbol* alias = m_prog.newSymbol(Symbol::Namespace, d->name, in);
        alias->members = target->members; alias->loc = d->loc;
        break;
    }
    case cxx::Decl::UsingDirective: {
        Symbol* target = resolveName(d->target, in, false, 0);
        if (!target || target->kind != Symbol::Namespace) { if (!m_reportedUnknown.count(d->target.text())) { m_reportedUnknown.insert(d->target.text()); error(d->loc, "S115", "unknown namespace '" + d->target.text() + "'"); } break; }
        in->usingDirectives.push_back(target->members);
        break;
    }
    case cxx::Decl::UsingDecl: {
        std::vector<Symbol*> all;
        resolveName(d->target, in, false, &all);
        if (all.empty()) { if (!m_reportedUnknown.count(d->target.text())) { m_reportedUnknown.insert(d->target.text()); error(d->loc, "S101", "unknown name '" + d->target.text() + "' in using declaration"); } break; }
        for (size_t i = 0; i < all.size(); ++i) in->add(all[i]);
        break;
    }
    case cxx::Decl::Class:
        d->semaSym = declareClass(d->classDef, in, access, 0);
        // "union { int i; double d; };" in a class: an unnamed member whose members belong to the class
        if (in->kind == Scope::Class && d->classDef->name.parts.empty() && d->classDef->isDefinition) declareAnonymousMember(d->semaSym, in, access);
        break;
    case cxx::Decl::Enum: d->semaSym = declareEnum(d->enumDef, in, access); break;
    case cxx::Decl::Simple: declareSimple(d, in, access); break;
    case cxx::Decl::Function: {
        const Type* base = typeOf(d->spec, in);
        std::vector<Symbol*> params;
        // the declarator's scope for parameter types: the class of an out-of-line member
        Scope* dscope = in;
        if (d->declarator && d->declarator->name.parts.size() > 1) { Scope* q = scopeForQualifier(d->declarator->name, in); if (q) dscope = q; }
        const Type* ft = d->declarator ? applyDeclarator(base, *d->declarator, dscope, &params) : m_types.error();
        Symbol* fn = declareFunction(d, d->declarator, d->spec, in, access, ft, params);
        if (!fn) break;
        d->semaSym = fn;
        if (fn->hasBody && fn->definition != d) { error(d->loc, "S110", "redefinition of '" + fn->qualifiedName() + "'"); break; }
        fn->hasBody = true; fn->definition = d; fn->isInline = fn->isInline || d->spec.isInline;
        if (!fn->isDependentContext) { PendingBody pb; pb.fn = fn; pb.def = d; pb.scope = fn->ownerClass ? fn->ownerClass->members : (dscope != in ? dscope : in); queueBody(pb); }
        break;
    }
    case cxx::Decl::Template: declareTemplate(d, in, access); break;
    case cxx::Decl::ExplicitInstantiation: {
        if (!d->decls.empty() && d->decls[0]->spec.kind == cxx::DeclSpec::Named) typeOf(d->decls[0]->spec, in);   // forces the instantiation
        break;
    }
    case cxx::Decl::LinkageSpec: {
        for (size_t i = 0; i < d->decls.size(); ++i) {
            declare(d->decls[i], in, access);
            // extern "C": plain names
            cxx::Decl* m = d->decls[i];
            if (d->name == "C") {
                if (m->kind == cxx::Decl::Function && m->semaSym) m->semaSym->isExternC = true;
                if (m->kind == cxx::Decl::Simple) for (size_t k = 0; k < m->declarators.size(); ++k) if (m->declarators[k].decl && m->declarators[k].decl->semaSym) m->declarators[k].decl->semaSym->isExternC = true;
            }
        }
        break;
    }
    case cxx::Decl::Friend: {
        // "friend class X;": X (declared somewhere this scope sees) may access the members; one not declared yet
        // leaves the class unchecked (7.3.1.2/3 would declare it in the enclosing namespace, invisible to lookup)
        Symbol* cls = in->kind == Scope::Class ? in->owner : 0;
        if (!cls) break;
        Symbol* f = lookupQualified(d->spec.name, in, true);
        if (f && (f->kind == Symbol::Class || f->kind == Symbol::ClassTemplate)) cls->friends.push_back(f);
        else cls->friendNames.push_back(d->spec.name.text());
        break;
    }
    case cxx::Decl::AccessLabel: case cxx::Decl::Empty: case cxx::Decl::Error: break;
    }
}

Symbol* Sema::declareEnum(cxx::EnumDecl* e, Scope* in, cxx::Access access)
{
    Symbol* en = 0;
    if (!e->name.empty()) {
        const std::vector<Symbol*>* v = in->find(e->name);
        if (v) for (size_t i = 0; i < v->size() && !en; ++i) if ((*v)[i]->kind == Symbol::Enum) en = (*v)[i];
    }
    if (!en) { en = m_prog.newSymbol(Symbol::Enum, e->name, in); en->loc = e->loc; en->access = access; en->type = m_types.enumType(en); en->size = 4; en->align = 4; en->layoutDone = true; }
    if (!e->isDefinition) return en;
    en->isDefinition = true;
    long next = 0;
    for (size_t i = 0; i < e->enumerators.size(); ++i) {
        const cxx::Enumerator& x = e->enumerators[i];
        Symbol* s = m_prog.newSymbol(Symbol::Enumerator, x.name, in);     // enumerators live in the enclosing scope
        s->loc = x.loc; s->access = access; s->type = en->type; s->ownerClass = in->kind == Scope::Class ? in->owner : 0;
        if (x.value) {
            expr(x.value, in);
            long v;
            // in a template's pattern the value may depend on the parameters (sizeof of a dependent type): checked per instantiation
            const bool dependent = in->kind == Scope::TemplateParams || (in->kind == Scope::Class && in->owner && in->owner->isDependentContext);
            if (constantValue(x.value, v)) next = v;
            else if (!dependent) error(x.loc, "S114", "enumerator value is not a constant");
        }
        s->hasConstValue = true; s->constValue = next; s->index = static_cast<int>(i);
        ++next;
    }
    return en;
}

// where 'target' occurs among the bases of 'from' (transitively): as a virtual base (the edge to it is virtual)
// or not; a non-virtual base of a virtual base counts as non-virtual, as cl takes it
static void baseOccurrences(const Symbol* from, const Symbol* target, bool& nonVirtual, bool& virt, int depth)
{
    if (!from || depth > 32) return;
    for (size_t b = 0; b < from->bases.size(); ++b) {
        const bool v = from->bases[b].isVirtual;   // cl looks at the edge to the base itself only
        if (from->bases[b].cls == target) (v ? virt : nonVirtual) = true;
        baseOccurrences(from->bases[b].cls, target, nonVirtual, virt, depth + 1);
    }
}

Symbol* Sema::declareClass(cxx::ClassDecl* c, Scope* in, cxx::Access access, Symbol* existing)
{
    Scope* target = in;
    std::string simple = c->name.parts.empty() ? std::string("") : c->name.parts.back().ident;
    if (c->name.parts.size() > 1) { Scope* q = scopeForQualifier(c->name, in); if (q) target = q; }
    Symbol* cls = existing;
    if (!cls && !simple.empty()) {
        std::vector<Symbol*> out;
        if (target->kind == Scope::Class) lookupMember(target->owner, simple, out);
        else { const std::vector<Symbol*>* v = target->find(simple); if (v) out = *v; }
        for (size_t i = 0; i < out.size() && !cls; ++i) if (out[i]->kind == Symbol::Class && out[i]->scope == target) cls = out[i];
    }
    if (!cls) {
        cls = m_prog.newSymbol(Symbol::Class, simple.empty() ? std::string("(anonymous)") : simple, target);
        cls->loc = c->loc; cls->access = access;
        cls->type = m_types.classType(cls);
        if (simple.empty()) cls->name = "";
    }
    cls->isStruct = c->key != "class"; cls->isUnion = c->key == "union";
    cls->classAst = c;
    // a class pattern of a template, or a class nested in one: its member bodies wait for an instantiation
    if (!existing) cls->isDependentContext = target->kind == Scope::TemplateParams || (target->kind == Scope::Class && target->owner && target->owner->isDependentContext);
    if (!c->isDefinition) return cls;
    if (cls->isDefinition) { error(c->loc, "S110", "redefinition of class '" + cls->qualifiedName() + "'"); return cls; }
    cls->isDefinition = true;
    cls->defOrder = m_bodyOrder >= 0 ? 0 : ++m_classOrder;
    const size_t firstPending = m_pending.size();
    const bool outermost = m_definingDepth++ == 0;
    cls->members = m_prog.newScope(Scope::Class, target, cls);
    ++m_stats.classes;
    // bases
    for (size_t b = 0; b < c->bases.size(); ++b) {
        const Type* bt = namedType(c->bases[b].name, in, "", c->bases[b].loc);
        BaseClass bc;
        bc.access = c->bases[b].access == cxx::AccessDefault ? (cls->isStruct ? cxx::AccessPublic : cxx::AccessPrivate) : c->bases[b].access;
        bc.isVirtual = c->bases[b].isVirtual;
        if (bt->kind == Type::Class && bt->decl) {
            if (!bt->decl->isDefinition) error(c->bases[b].loc, "S113", "base class '" + bt->decl->qualifiedName() + "' is incomplete");
            bc.cls = bt->decl;
            cls->bases.push_back(bc);
        } else if (bt->kind == Type::Dependent) {
            // a dependent base: members are looked up at instantiation time
        } else if (bt->kind != Type::Error) error(c->bases[b].loc, "S113", "base '" + c->bases[b].name.text() + "' is not a class");
    }
    // a direct base that is also a base of another direct base, as cl reports it: an error (C2584) when the
    // direct one comes first and only one of the two is virtual, else a warning (C4584) when the direct one is not
    // virtual; nothing when both are virtual (one shared subobject) or for a later virtual direct base
    for (size_t i = 0; i < cls->bases.size(); ++i)
        for (size_t j = 0; j < cls->bases.size(); ++j) {
            if (i == j || !cls->bases[i].cls || !cls->bases[j].cls) continue;
            bool nonVirtual = false, virt = false;
            baseOccurrences(cls->bases[j].cls, cls->bases[i].cls, nonVirtual, virt, 0);
            if (!nonVirtual && !virt) continue;
            const bool direct = cls->bases[i].isVirtual, indirect = virt && !nonVirtual;
            if (direct && indirect) continue;
            const std::string d = cls->bases[i].cls->qualifiedName(), o = cls->bases[j].cls->qualifiedName();
            if (i < j && direct != indirect) error(c->loc, "S141", "'" + cls->qualifiedName() + "': direct base '" + d + "' is inaccessible; already a base of '" + o + "' (cl C2584)");
            else if (!direct) warning(c->loc, "S142", "'" + cls->qualifiedName() + "': base-class '" + d + "' is already a base-class of '" + o + "' (cl C4584)");
        }
    // members: the class is complete for member function bodies, which are analysed later
    Symbol* savedClass = m_currentClass;
    m_currentClass = cls;
    declareAll(c->members, cls->members, cls->isStruct ? cxx::AccessPublic : cxx::AccessPrivate);
    m_currentClass = savedClass;
    completeClass(cls);
    --m_definingDepth;
    if (outermost && m_bodyOrder < 0) for (size_t k = firstPending; k < m_pending.size(); ++k) m_pending[k].order = m_classOrder;
    return cls;
}

// an anonymous union or struct inside a class: an unnamed member of that type holds it, and every member of it
// (nested anonymous members included) is named in the class by a symbol placed at the same bytes and bits;
// layoutClass sets the offsets once the unnamed member has its own
void Sema::declareAnonymousMember(Symbol* anon, Scope* in, cxx::Access access)
{
    Symbol* owner = in->owner;
    if (!owner || !anon || !anon->members) return;
    Symbol* host = m_prog.newSymbol(Symbol::Variable, "", 0);
    host->type = anon->type; host->loc = anon->loc; host->access = access; host->isMember = true; host->ownerClass = owner;
    host->isDefinition = true; host->isDependentContext = owner->isDependentContext;
    host->index = static_cast<int>(owner->fields.size());
    owner->fields.push_back(host);
    for (size_t i = 0; i < anon->members->order.size(); ++i) {
        Symbol* m = anon->members->order[i];
        if (m->kind != Symbol::Variable || !m->isMember || m->isStatic || m->name.empty()) continue;
        Symbol* p = m_prog.newSymbol(Symbol::Variable, m->name, in);
        p->type = m->type; p->loc = m->loc; p->access = access; p->isMember = true; p->ownerClass = owner; p->isMutable = m->isMutable;
        p->isDefinition = true; p->isDependentContext = owner->isDependentContext; p->declAst = m->declAst; p->index = -1;
        p->bitWidth = m->bitWidth; p->bitOffset = m->bitOffset; p->anonHost = host; p->anonOf = m;
    }
}

namespace {

bool sameSignature(const Type* a, const Type* b);

// the offset of base in derived along non-virtual bases only (false when there is none, or only through a
// virtual base)
bool nonVirtualOffset(const Symbol* derived, const Symbol* base, int& off, int depth)
{
    if (!derived || !base || depth > 32) return false;
    if (derived == base) { off = 0; return true; }
    for (size_t i = 0; i < derived->bases.size(); ++i) {
        int o = 0;
        if (derived->bases[i].isVirtual || !nonVirtualOffset(derived->bases[i].cls, base, o, depth + 1)) continue;
        off = derived->bases[i].offset + o;
        return true;
    }
    return false;
}

// the offset of a subobject of class owner in a complete object of cls: through non-virtual bases, or in one of the
// virtual bases (at its place in cls)
bool completeOffset(const Symbol* cls, const Symbol* owner, int& off)
{
    if (nonVirtualOffset(cls, owner, off, 0)) return true;
    for (size_t v = 0; v < cls->vbases.size() && v < cls->vbaseOffsets.size(); ++v) {
        int o = 0;
        if (nonVirtualOffset(cls->vbases[v], owner, o, 0)) { off = cls->vbaseOffsets[v] + o; return true; }
    }
    return false;
}

// whether m (declared in a class deriving from vb) overrides a virtual function other than a destructor held by one
// of vb's own vftables (not those inside vb's virtual bases: their functions are those bases')
bool overridesFunctionOf(const Symbol* m, const Symbol* vb)
{
    if (!m || m->kind != Symbol::Function || m->isStatic || m->isDestructor || m->isConstructor) return false;
    for (size_t t = 0; t < vb->vftables.size(); ++t)
        for (size_t s = 0; !vb->vftables[t].vbase && s < vb->vftables[t].fns.size(); ++s) {
            const Symbol* v = vb->vftables[t].fns[s];
            if (v && !v->isDestructor && v->name == m->name && sameSignature(v->type, m->type)) return true;
        }
    return false;
}

// whether a member function declared virtual overrides some virtual function of the bases (then it needs no vfptr
// of its own: cl gives a class a vfptr only for new virtual functions)
bool overridesAny(const Symbol* m, const Symbol* cls)
{
    for (size_t b = 0; b < cls->bases.size(); ++b) {
        const Symbol* bc = cls->bases[b].cls;
        if (!bc) continue;
        for (size_t t = 0; t < bc->vftables.size(); ++t)
            for (size_t s = 0; s < bc->vftables[t].fns.size(); ++s) {
                const Symbol* v = bc->vftables[t].fns[s];
                if (v && (m->isDestructor ? v->isDestructor : (!v->isDestructor && v->name == m->name && sameSignature(v->type, m->type)))) return true;
            }
    }
    return false;
}

// the move of a covariant result from the class overrider m returns (a pointer or reference to) to the one root,
// the overridden function, returns: 0 when they are the same class or the base is at offset 0, -1 when the base is
// reached through a virtual base
int returnAdjustment(const Symbol* m, const Symbol* root)
{
    if (!m->type || !root->type || m->type->kind != Type::Function || root->type->kind != Type::Function) return 0;
    const Type* r = m->type->inner;
    const Type* o = root->type->inner;
    if (!r || !o || (r->kind != Type::Pointer && r->kind != Type::Reference) || r->kind != o->kind) return 0;
    const Type* rc = r->inner;
    const Type* oc = o->inner;
    if (!rc || !oc || rc->kind != Type::Class || oc->kind != Type::Class || !rc->decl || !oc->decl || rc->decl == oc->decl) return 0;
    int off = 0;
    if (nonVirtualOffset(rc->decl, oc->decl, off, 0)) return off;
    return -1;
}

bool sameSignature(const Type* a, const Type* b)
{
    if (!a || !b || a->kind != Type::Function || b->kind != Type::Function) return false;
    if (a->params.size() != b->params.size() || a->variadic != b->variadic || a->constMethod != b->constMethod) return false;
    for (size_t i = 0; i < a->params.size(); ++i) {
        const Type* x = a->params[i]; const Type* y = b->params[i];
        // top-level cv of parameters does not take part in the signature
        if (x->kind != Type::Reference && x->kind != Type::Pointer) { if (x->isConst || x->isVolatile) x = 0; }
        if (y->kind != Type::Reference && y->kind != Type::Pointer) { if (y->isConst || y->isVolatile) y = 0; }
        if (x && y && x != y) return false;
        if ((!x || !y) && a->params[i]->kind != b->params[i]->kind) return false;
    }
    return true;
}

// the written form of a function template's declaration, for matching a redeclaration or the definition with an
// earlier declaration (14.5.5.1: the same template parameters, result and parameter types). Parameter names are
// left out, template parameter names must agree, and template arguments that are expressions keep their text
// ("Matrix<T, R - 1, C - 1>"), so two overloads that differ only there stay apart
std::string nameShape(const cxx::QualifiedName& n);

std::string specShape(const cxx::DeclSpec& s)
{
    if (s.kind != cxx::DeclSpec::Named) return s.text();
    return std::string(s.isConst ? "const " : "") + (s.isVolatile ? "volatile " : "") + nameShape(s.name);
}

std::string declaratorShape(const cxx::Declarator* d)
{
    if (!d) return "";
    cxx::Declarator unnamed = *d;
    unnamed.name = cxx::QualifiedName();
    return unnamed.text();
}

std::string nameShape(const cxx::QualifiedName& n)
{
    std::string s = n.global ? "::" : "";
    for (size_t i = 0; i < n.parts.size(); ++i) {
        const cxx::NameComponent& c = n.parts[i];
        s += (i ? "::" : "") + c.ident;
        if (!c.hasTemplateArgs) continue;
        s += "<";
        for (size_t a = 0; a < c.templateArgs.size(); ++a) {
            const cxx::TemplateArg& ta = c.templateArgs[a];
            s += a ? ", " : "";
            if (ta.type) s += specShape(ta.type->spec) + " " + declaratorShape(ta.type->decl);
            else s += cxx::expressionText(ta.expr);
        }
        s += ">";
    }
    return s;
}

std::string templateSignature(const cxx::Decl* tmpl, const cxx::Decl* inner, const cxx::Declarator* dcl)
{
    std::string s = "<";
    for (size_t i = 0; i < tmpl->templateParams.size(); ++i) {
        const cxx::TemplateParam* p = tmpl->templateParams[i];
        s += (i ? ", " : "") + std::string(p->kind == cxx::TemplateParam::Type ? "typename " : p->kind == cxx::TemplateParam::Template ? "template " : "");
        if (p->kind == cxx::TemplateParam::NonType && p->nonType) s += specShape(p->nonType->spec) + " " + declaratorShape(p->nonType->decl) + " ";
        s += p->name;
    }
    // the result and the function's own declarator parts (const, variadic), then the parameters one by one
    cxx::Declarator outer = *dcl;
    if (!outer.parts.empty()) outer.parts.back().params.clear();
    s += "> " + specShape(inner->spec) + " " + declaratorShape(&outer);
    const cxx::DeclaratorPart* fp = dcl->functionPart();
    for (size_t k = 0; fp && k < fp->params.size(); ++k) s += " | " + specShape(fp->params[k]->spec) + " " + declaratorShape(fp->params[k]->decl);
    return s;
}

} // namespace

Symbol* Sema::declareFunction(cxx::Decl* d, cxx::Declarator* dcl, const cxx::DeclSpec& spec, Scope* in, cxx::Access access, const Type* ft, const std::vector<Symbol*>& params)
{
    if (!dcl || ft->kind != Type::Function) return 0;
    const cxx::QualifiedName& n = dcl->name;
    std::string name = n.parts.back().ident;
    if (spec.isFriend && in->kind == Scope::Class && in->owner && n.parts.size() > 1) in->owner->friendsUnknown = true;   // "friend void N::f();", "friend void B::g();": not resolved here
    if (n.parts.back().convType) name = "operator " + n.parts.back().convType->text();
    Scope* target = in;
    Symbol* owner = in->kind == Scope::Class ? in->owner : 0;
    // a friend function declared (or defined) in a class is a function of the innermost enclosing namespace
    // (11.4/5, 7.3.1.2/3), not a member; found only by argument-dependent lookup until that namespace declares it
    const bool friendInClass = spec.isFriend && in->kind == Scope::Class && n.parts.size() == 1;
    if (friendInClass) {
        while (target && !target->isNamespaceLike()) target = target->parent;
        if (!target) target = m_prog.global;
        owner = 0;
    }
    if (n.parts.size() > 1) {
        Scope* q = scopeForQualifier(n, in);
        if (!q) { if (!m_reportedUnknown.count(n.text())) { m_reportedUnknown.insert(n.text()); error(dcl->loc, "S101", "unknown scope in '" + n.text() + "'"); } return 0; }
        target = q;
        owner = q->kind == Scope::Class ? q->owner : 0;
    }
    bool ctor = owner && name == owner->ctorName() && !n.parts.back().hasTemplateArgs;
    bool dtor = owner && !name.empty() && name[0] == '~';
    bool conv = n.parts.back().convType != 0;
    if (ctor || dtor) ft = m_types.function(m_types.basic(Type::Void), ft->params, ft->variadic, ft->constMethod);
    else if (conv) {
        const cxx::TypeId* ct = n.parts.back().convType;
        const Type* rt = typeOf(ct->spec, target);
        if (ct->decl) rt = applyDeclarator(rt, *ct->decl, target, 0);
        ft = m_types.function(rt, ft->params, ft->variadic, ft->constMethod);
    }
    // an existing declaration with the same signature?
    std::vector<Symbol*> existing;
    if (target->kind == Scope::Class) { const std::vector<Symbol*>* v = target->find(name); if (v) existing = *v; }
    else { const std::vector<Symbol*>* v = target->find(name); if (v) existing = *v; }
    for (size_t i = 0; i < existing.size(); ++i) {
        Symbol* e = existing[i];
        if (e->kind == Symbol::Function && sameSignature(e->type, ft)) {
            if (n.parts.size() > 1 || e->scope == target) {
                dcl->semaSym = e;
                if (spec.isFriend && in->kind == Scope::Class && in->owner) in->owner->friends.push_back(e);
                if (spec.noreturn) e->isNoreturn = true;
                if (!friendInClass && !spec.isFriend) e->isFriendDecl = false;   // the namespace declares it: visible to ordinary lookup now
                // an out-of-line definition supplies the parameter names
                if (!params.empty() && e->params.size() == params.size()) for (size_t k = 0; k < params.size(); ++k) if (e->params[k]->name.empty()) e->params[k]->name = params[k]->name;
                if (!params.empty()) {
                    // default arguments given by the declaration stay with the function
                    if (e->params.size() == params.size()) for (size_t k = 0; k < params.size(); ++k) if (!params[k]->init && e->params[k]->init) params[k]->init = e->params[k]->init;
                    e->params = params;
                }
                return e;
            }
        }
    }
    if (n.parts.size() > 1 && !existing.empty() && target->kind == Scope::Class) {
        bool anyFn = false;
        for (size_t i = 0; i < existing.size(); ++i) if (existing[i]->kind == Symbol::Function) anyFn = true;
        if (anyFn) { error(dcl->loc, "S116", "'" + n.text() + "' does not match any declaration in its class"); }
    } else if (n.parts.size() > 1 && target->kind == Scope::Class) {
        error(dcl->loc, "S116", "'" + n.text() + "' is not a member of its class");
    }
    Symbol* fn = m_prog.newSymbol(Symbol::Function, name, target);
    fn->loc = dcl->loc; fn->access = access; fn->type = ft; fn->params = params; fn->declAst = d; fn->ownerClass = owner;
    fn->isStatic = spec.isStatic; fn->isExtern = spec.isExtern; fn->isExplicit = spec.isExplicit;
    fn->isInline = spec.isInline || (in->kind == Scope::Class && d->kind == cxx::Decl::Function);   // defined in the class body
    fn->isVirtual = spec.isVirtual; fn->isConstructor = ctor; fn->isDestructor = dtor; fn->isConversion = conv;
    fn->isOperator = name.compare(0, 8, "operator") == 0 && !conv;
    fn->isFriendDecl = friendInClass;
    fn->isNoreturn = spec.noreturn;
    if (spec.isFriend && in->kind == Scope::Class && in->owner) in->owner->friends.push_back(fn);
    fn->isDependentContext = in->kind == Scope::TemplateParams || (owner && owner->isDependentContext) || (friendInClass && in->owner && in->owner->isDependentContext);
    fn->requiredParams = static_cast<int>(params.size());
    for (size_t k = 0; k < params.size(); ++k) if (params[k]->init) { fn->requiredParams = static_cast<int>(k); break; }
    // default arguments are analysed with the bodies, in the class or declaring scope (a member may be declared later)
    if (!fn->isDependentContext) for (size_t k = 0; k < params.size(); ++k) if (params[k]->init) { PendingBody pb; pb.fn = params[k]; pb.def = d; pb.scope = owner && owner->members ? owner->members : target; queueBody(pb); }
    for (size_t k = 0; k < params.size(); ++k) params[k]->scope = 0;
    if (owner) {
        if (ctor) { owner->hasUserCtor = true; if (params.empty() || fn->requiredParams == 0) owner->hasDefaultCtor = true;
                    if (params.size() >= 1 && fn->requiredParams <= 1) { const Type* p0 = params[0]->type; if (p0->kind == Type::Reference && m_types.unqualified(p0->inner) == owner->type) owner->hasCopyCtor = true; } }
        if (dtor) owner->hasDtor = true;
        if (fn->isOperator && name == "operator=" && params.size() == 1) { const Type* p0 = params[0]->type; if ((p0->kind == Type::Reference && m_types.unqualified(p0->inner) == owner->type) || m_types.unqualified(p0) == owner->type) owner->hasCopyAssign = true; }
    }
    dcl->semaSym = fn;
    ++m_stats.symbols;
    return fn;
}

void Sema::declareSimple(cxx::Decl* d, Scope* in, cxx::Access access)
{
    // an inline class / enum definition in the specifiers
    Symbol* defined = 0;
    if (d->spec.kind == cxx::DeclSpec::Class && d->spec.classDef) { defined = declareClass(d->spec.classDef, in, access, 0); d->semaSym = defined; }
    else if (d->spec.kind == cxx::DeclSpec::Enum && d->spec.enumDef) { defined = declareEnum(d->spec.enumDef, in, access); d->semaSym = defined; }
    else if (d->spec.kind == cxx::DeclSpec::Named && !d->spec.elaborated.empty() && d->spec.elaborated != "typename" && d->spec.elaborated != "enum" && d->declarators.empty()) {
        if (d->spec.isFriend && in->kind == Scope::Class && in->owner) {
            // "friend class X<T>;", "friend class N::Y;": the class named, when it can be found; else unchecked
            Symbol* f = lookupQualified(d->spec.name, in, true);
            if (d->spec.name.parts.back().hasTemplateArgs) in->owner->friendsUnknown = true;
            else if (f && (f->kind == Symbol::Class || f->kind == Symbol::ClassTemplate)) in->owner->friends.push_back(f);
            else in->owner->friendNames.push_back(d->spec.name.text());
            return;
        }
        // "class X;" declares X in this scope unless this scope has it already: an X of an outer scope does not count
        if (d->spec.name.parts.size() == 1 && !d->spec.name.global) {
            const std::string& nm = d->spec.name.parts[0].ident;
            Symbol* here = 0;
            const std::vector<Symbol*>* v = in->find(nm);
            if (v) for (size_t k = 0; k < v->size() && !here; ++k) if ((*v)[k]->kind == Symbol::Class || (*v)[k]->kind == Symbol::ClassTemplate) here = (*v)[k];
            if (!here) {
                Symbol* cls = m_prog.newSymbol(Symbol::Class, nm, in);
                cls->loc = d->spec.loc; cls->access = access; cls->type = m_types.classType(cls);
                cls->isStruct = d->spec.elaborated != "class"; cls->isUnion = d->spec.elaborated == "union";
                cls->isDependentContext = in->kind == Scope::TemplateParams || (in->kind == Scope::Class && in->owner && in->owner->isDependentContext);
                d->semaSym = cls;
            }
            return;
        }
        namedType(d->spec.name, in, d->spec.elaborated, d->spec.loc);
        return;
    }
    if (d->declarators.empty()) return;
    const Type* base;
    if (defined) base = m_types.qualified(defined->kind == Symbol::Class ? m_types.classType(defined) : m_types.enumType(defined), d->spec.isConst, d->spec.isVolatile);
    else base = typeOf(d->spec, in);

    for (size_t i = 0; i < d->declarators.size(); ++i) {
        cxx::InitDeclarator& id = d->declarators[i];
        cxx::Declarator* dcl = id.decl;
        if (!dcl) continue;
        Scope* dscope = in;
        if (dcl->name.parts.size() > 1) { Scope* q = scopeForQualifier(dcl->name, in); if (q) dscope = q; }
        std::vector<Symbol*> params;
        const Type* t = completedArray(applyDeclarator(base, *dcl, dscope, &params), id.init);
        const std::string& name = dcl->name.parts.empty() ? std::string("") : dcl->name.parts.back().ident;
        if (d->spec.isTypedef) {
            Symbol* td = m_prog.newSymbol(Symbol::Typedef, name, in);
            td->type = t; td->loc = dcl->loc; td->access = access; dcl->semaSym = td;
            continue;
        }
        if (t->kind == Type::Function) {
            Symbol* fn = declareFunction(d, dcl, d->spec, in, access, t, params);
            if (fn && id.pureVirtual) { fn->isPureVirtual = true; fn->isVirtual = true; }
            continue;
        }
        // a bit-field: its width is a constant; an unnamed one (": n") only shapes the layout
        long width = -1;
        if (dcl->bitfield && in->kind == Scope::Class && in->owner) {
            if (!in->owner->isDependentContext) {
                expr(dcl->bitfield, in);
                const int bits = 8 * m_types.sizeOf(t);
                if (!t->isIntegral()) error(dcl->loc, "S105", "bit-field '" + name + "' must have an integral or enumeration type");
                else if (!constantValue(dcl->bitfield, width)) { error(dcl->loc, "S114", "bit-field width is not a constant"); width = -1; }
                else if (width < 0 || width > bits || (width == 0 && !name.empty())) { error(dcl->loc, "S114", "bit-field width " + intToString(static_cast<int>(width)) + " is out of range for " + t->text()); width = -1; }
            }
            if (name.empty()) {
                if (width >= 0) {
                    Symbol* u = m_prog.newSymbol(Symbol::Variable, "", 0);
                    u->type = t; u->loc = dcl->loc; u->isMember = true; u->ownerClass = in->owner; u->bitWidth = static_cast<int>(width);
                    u->index = static_cast<int>(in->owner->fields.size());
                    in->owner->unnamedBitFields.push_back(u);
                }
                continue;
            }
        }
        // variable, field or static member
        Scope* target = in;
        Symbol* var = 0;
        if (dcl->name.parts.size() > 1) {
            // out-of-line definition of a static data member
            Scope* q = scopeForQualifier(dcl->name, in);
            if (q) { std::vector<Symbol*> out; if (q->kind == Scope::Class) lookupMember(q->owner, name, out); for (size_t k = 0; k < out.size() && !var; ++k) if (out[k]->kind == Symbol::Variable) var = out[k]; }
            if (!var) { error(dcl->loc, "S116", "'" + dcl->name.text() + "' is not a static member of its class"); continue; }
            var->isDefinition = true;
            if (var->type && var->type->kind == Type::Array && var->type->arraySize < 0 && t->kind == Type::Array) var->type = t;
        } else {
            // "extern int x;" after "int x;" refers to the same variable
            const std::vector<Symbol*>* v = target->find(name);
            if (v && target->isNamespaceLike()) for (size_t k = 0; k < v->size() && !var; ++k) if ((*v)[k]->kind == Symbol::Variable) var = (*v)[k];
            if (!var) {
                var = m_prog.newSymbol(Symbol::Variable, name, target);
                var->type = t; var->loc = dcl->loc; var->access = access;
                var->isStatic = d->spec.isStatic; var->isExtern = d->spec.isExtern; var->isMutable = d->spec.isMutable;
                var->isMember = target->kind == Scope::Class;
                var->ownerClass = var->isMember ? target->owner : 0;
                if (var->isMember && !var->isStatic) { var->index = static_cast<int>(target->owner->fields.size()); target->owner->fields.push_back(var); }
                var->isDependentContext = in->kind == Scope::TemplateParams || (var->ownerClass && var->ownerClass->isDependentContext);
                ++m_stats.variables;
            }
            var->isDefinition = var->isDefinition || !d->spec.isExtern;
        }
        dcl->semaSym = var;
        if (width >= 0 && var->isMember && !var->isStatic) var->bitWidth = static_cast<int>(width);
        if (d->spec.align > var->declAlign) var->declAlign = d->spec.align;
        if (!var->declAst || id.init.kind != cxx::Initializer::None) var->declAst = d;   // the declaration carrying the initializer
        if (id.init.kind != cxx::Initializer::None) {
            var->init = id.init.args.empty() ? 0 : id.init.args[0];
            if (!var->isDependentContext && !var->isMember) { PendingBody pb; pb.fn = var; pb.def = d; pb.scope = in; queueBody(pb); }
            else if (var->isMember && var->isStatic && var->init && !var->isDependentContext) { PendingBody pb; pb.fn = var; pb.def = d; pb.scope = in; queueBody(pb); }
        }
    }
}

void Sema::declareTemplate(cxx::Decl* d, Scope* in, cxx::Access access)
{
    if (d->decls.empty()) return;
    cxx::Decl* inner = d->decls[0];
    if (inner->spec.isFriend && in->kind == Scope::Class && in->owner) { in->owner->friendsUnknown = true; return; }   // a friend template: not checked
    // the template parameter scope
    Scope* tscope = m_prog.newScope(Scope::TemplateParams, in, 0);
    std::vector<Symbol*> tparams;
    for (size_t i = 0; i < d->templateParams.size(); ++i) {
        const cxx::TemplateParam* p = d->templateParams[i];
        Symbol* ps = m_prog.newSymbol(Symbol::TemplateParam, p->name.empty() ? std::string("$T") : p->name, tscope);
        ps->loc = p->loc; ps->index = static_cast<int>(i);
        if (p->kind == cxx::TemplateParam::NonType) { ps->kind = Symbol::Variable; ps->type = p->nonType ? typeOf(p->nonType->spec, tscope) : m_types.basic(Type::Int); ps->isDependentContext = true; ps->hasConstValue = false; ps->init = p->nonType ? p->nonType->defaultValue : 0; ps->kind = Symbol::TemplateParam; }
        else ps->type = m_types.dependent(ps);
        tparams.push_back(ps);
    }
    if (inner->kind == cxx::Decl::Template) { declareTemplate(inner, tscope, access); return; }   // member template of a template

    // which entity?
    cxx::ClassDecl* classAst = 0;
    if (inner->kind == cxx::Decl::Class) classAst = inner->classDef;
    else if (inner->kind == cxx::Decl::Simple && inner->spec.kind == cxx::DeclSpec::Class && inner->declarators.empty()) classAst = inner->spec.classDef;
    if (!classAst && inner->kind == cxx::Decl::Simple && inner->spec.kind == cxx::DeclSpec::Named && inner->declarators.empty()
        && (inner->spec.elaborated == "class" || inner->spec.elaborated == "struct" || inner->spec.elaborated == "union")
        && inner->spec.name.parts.size() == 1 && !inner->spec.name.parts[0].hasTemplateArgs) {
        // "template <...> class X;" declares the template without a definition: its specialisations may follow,
        // and an instantiation before a definition is an incomplete class
        const std::string& name = inner->spec.name.parts[0].ident;
        Symbol* ct = 0;
        const std::vector<Symbol*>* v = in->find(name);
        if (v) for (size_t i = 0; i < v->size() && !ct; ++i) if ((*v)[i]->kind == Symbol::ClassTemplate && !(*v)[i]->patternArgs.size()) ct = (*v)[i];
        if (!ct) {
            ct = m_prog.newSymbol(Symbol::ClassTemplate, name, in);
            ct->loc = d->loc; ct->access = access; ct->templateAst = d; ct->templateParams = tparams; ct->isDependentContext = true;
            tscope->owner = ct;
            ++m_stats.symbols;
        }
        d->semaSym = ct; inner->semaSym = ct;
        return;
    }
    if (classAst) {
        std::string name = classAst->name.parts.empty() ? std::string("") : classAst->name.parts.back().ident;
        bool specialization = !classAst->name.parts.empty() && classAst->name.parts.back().hasTemplateArgs;
        Symbol* primary = 0;
        {
            std::vector<Symbol*> out;
            Scope* target = in;
            if (classAst->name.parts.size() > 1) { Scope* q = scopeForQualifier(classAst->name, in); if (q) target = q; }
            const std::vector<Symbol*>* v = target->find(name);
            if (v) for (size_t i = 0; i < v->size() && !primary; ++i) if ((*v)[i]->kind == Symbol::ClassTemplate && !(*v)[i]->patternArgs.size()) primary = (*v)[i];
            if (!specialization) {
                Symbol* ct = primary;
                if (!ct) { ct = m_prog.newSymbol(Symbol::ClassTemplate, name, target); ct->loc = d->loc; ct->access = access; ++m_stats.symbols; }
                ct->templateAst = d; ct->templateParams = tparams; ct->classAst = classAst; ct->isDependentContext = true;
                tscope->owner = ct;
                if (classAst->isDefinition) ct->isDefinition = true;
                d->semaSym = ct; inner->semaSym = ct;
                return;
            }
            if (!primary) { error(d->loc, "S111", "specialisation of unknown template '" + name + "'"); return; }
            if (d->explicitSpecialization) {
                // a full specialisation is an ordinary class registered under its argument text
                std::vector<const Type*> types; std::vector<long> ints; std::string key;
                if (!templateArgText(classAst->name.parts.back().templateArgs, in, types, ints, key)) { error(d->loc, "S111", "bad specialisation arguments"); return; }
                cxx::ClassDecl copy = *classAst;
                copy.name.parts.back().hasTemplateArgs = false;
                copy.name.parts.back().ident = name + key;
                Scope* iscope = m_prog.newScope(Scope::Instantiation, in, primary);
                Symbol* cls = m_prog.newSymbol(Symbol::Class, name + key, iscope);
                cls->type = m_types.classType(cls); cls->instantiatedFrom = primary; cls->templateArgs = types; cls->templateIntArgs = ints;
                classAst->name.parts.back().ident = name + key; classAst->name.parts.back().hasTemplateArgs = false;   // so that out-of-line members match
                declareClass(classAst, iscope, access, cls);
                primary->instantiations[key] = cls;
                d->semaSym = cls;
                return;
            }
            // partial specialisation: keep the pattern for matching at instantiation time
            Symbol* ps = m_prog.newSymbol(Symbol::ClassTemplate, name, tscope);
            ps->templateAst = d; ps->templateParams = tparams; ps->classAst = classAst; ps->isDependentContext = true;
            ps->patternArgs = classAst->name.parts.back().templateArgs; ps->instantiatedFrom = primary;
            tscope->owner = ps;
            primary->partialSpecs.push_back(ps);
            d->semaSym = ps;
            return;
        }
    }
    // function template (definition or declaration), or a static member of a class template etc.
    cxx::Declarator* dcl = 0;
    if (inner->kind == cxx::Decl::Function) dcl = inner->declarator;
    else if (inner->kind == cxx::Decl::Simple && !inner->declarators.empty()) dcl = inner->declarators[0].decl;
    if (dcl && dcl->isFunction() && d->explicitSpecialization) { declareFunctionSpecialization(d, inner, dcl, in, access); return; }
    if (dcl && dcl->isFunction()) {
        std::string name = dcl->name.parts.back().ident;
        Scope* target = in;
        if (dcl->name.parts.size() > 1) {
            // member of a class template defined out of line: attach to the template so instantiation finds it
            Symbol* ct = 0;
            std::vector<Symbol*> out;
            cxx::QualifiedName q = dcl->name; q.parts.pop_back();
            if (q.parts.size() == 1) { lookupUnqualified(q.parts[0].ident, in, false, &out); for (size_t i = 0; i < out.size() && !ct; ++i) if (out[i]->kind == Symbol::ClassTemplate) ct = out[i]; }
            if (ct) { Symbol* mt = m_prog.newSymbol(Symbol::FunctionTemplate, name, tscope); mt->templateAst = d; mt->templateParams = tparams; mt->declAst = inner; mt->isDependentContext = true; mt->ownerClass = ct; ct->partialSpecs.push_back(mt); tscope->owner = mt; d->semaSym = mt; return; }
            Scope* qs = scopeForQualifier(dcl->name, in);
            if (qs) target = qs;
        }
        // a redeclaration, or the definition after a forward declaration, is the same template (one candidate):
        // the definition takes the symbol over, keeping the default arguments only a declaration gave
        const std::string sig = templateSignature(d, inner, dcl);
        const std::vector<Symbol*>* same = target->find(name);
        for (size_t i = 0; same && i < same->size(); ++i) {
            Symbol* ex = (*same)[i];
            if (ex->kind != Symbol::FunctionTemplate || ex->scope != target || !ex->templateAst || !ex->declAst) continue;
            const cxx::Declarator* exd = ex->declAst->kind == cxx::Decl::Function ? ex->declAst->declarator : (ex->declAst->declarators.empty() ? 0 : ex->declAst->declarators[0].decl);
            if (!exd || !exd->isFunction() || templateSignature(ex->templateAst, ex->declAst, exd) != sig) continue;
            if (inner->kind == cxx::Decl::Function) {
                if (ex->declAst->kind == cxx::Decl::Function) { error(d->loc, "S110", "redefinition of function template '" + ex->qualifiedName() + "'"); d->semaSym = ex; return; }
                std::vector<cxx::Param*>& np = dcl->parts.back().params;
                const std::vector<cxx::Param*>& op = exd->parts.back().params;
                for (size_t k = 0; k < np.size() && k < op.size(); ++k) if (!np[k]->defaultValue) np[k]->defaultValue = op[k]->defaultValue;
                ex->templateAst = d; ex->declAst = inner; ex->templateParams = tparams;
                tscope->owner = ex;
            }
            d->semaSym = ex;
            return;
        }
        Symbol* ft = m_prog.newSymbol(Symbol::FunctionTemplate, name, target);
        ft->loc = d->loc; ft->access = access; ft->templateAst = d; ft->templateParams = tparams; ft->declAst = inner; ft->isDependentContext = true;
        ft->ownerClass = target->kind == Scope::Class ? target->owner : 0;
        tscope->owner = ft;
        d->semaSym = ft;
        ++m_stats.symbols;
        return;
    }
    // other templates (static data members of class templates, typedef templates do not exist in C++98): ignore
}


// an explicit specialisation (14.7.3): "template <> R f<args>(params)" or "template <> R f(params)" of a function
// template (the arguments deduced from the parameter types), registered as the instantiation for those arguments
// so that calls find it instead of the template's body; "template <> R C<args>::m(params)" gives the member of
// that instantiation this definition instead of the template's
void Sema::declareFunctionSpecialization(cxx::Decl* d, cxx::Decl* inner, cxx::Declarator* dcl, Scope* in, cxx::Access access)
{
    const std::string name = dcl->name.parts.back().ident;
    if (dcl->name.parts.size() > 1) {
        Scope* q = scopeForQualifier(dcl->name, in);
        if (!q || q->kind != Scope::Class || !q->owner) { error(d->loc, "S111", "unknown class in the explicit specialisation '" + dcl->name.text() + "'"); return; }
        const Type* base = typeOf(inner->spec, q);
        std::vector<Symbol*> params;
        const Type* ft = applyDeclarator(base, *dcl, q, &params);
        Symbol* fn = 0;
        const std::vector<Symbol*>* v = q->find(name);
        if (v) for (size_t k = 0; k < v->size() && !fn; ++k) if ((*v)[k]->kind == Symbol::Function && sameSignature((*v)[k]->type, ft)) fn = (*v)[k];
        if (!fn) { error(dcl->loc, "S116", "'" + dcl->name.text() + "' does not match any member of its class"); return; }
        if (fn->body) { error(d->loc, "S110", "explicit specialisation of '" + fn->qualifiedName() + "' after its use"); return; }
        // the body the instantiation got from the template is dropped for this one
        for (size_t i = 0; i < m_pending.size(); ++i) if (m_pending[i].fn == fn) { m_pending.erase(m_pending.begin() + static_cast<std::ptrdiff_t>(i)); --i; }
        if (!params.empty() && fn->params.size() == params.size()) { for (size_t k = 0; k < params.size(); ++k) params[k]->scope = 0; fn->params = params; }
        fn->definition = inner; fn->hasBody = true; fn->declAst = inner; fn->isLazyInstantiation = false; fn->instantiationFailed = false; fn->isDependentContext = false;
        PendingBody pb; pb.fn = fn; pb.def = inner; pb.scope = q; queueBody(pb);
        dcl->semaSym = fn; d->semaSym = fn; inner->semaSym = fn;
        return;
    }
    const Type* base = typeOf(inner->spec, in);
    std::vector<Symbol*> params;
    const Type* ft = applyDeclarator(base, *dcl, in, &params);
    if (!ft || ft->kind != Type::Function) return;
    const std::vector<Symbol*>* cands = in->find(name);
    std::vector<Symbol*> primaries;
    if (cands) for (size_t k = 0; k < cands->size(); ++k) if ((*cands)[k]->kind == Symbol::FunctionTemplate && !(*cands)[k]->ownerClass) primaries.push_back((*cands)[k]);
    if (primaries.empty()) { error(d->loc, "S111", "explicit specialisation of unknown function template '" + name + "'"); return; }
    for (size_t t = 0; t < primaries.size(); ++t) {
        Symbol* tmpl = primaries[t];
        cxx::Decl* tdecl = tmpl->declAst;
        cxx::Declarator* tdcl = tdecl ? (tdecl->kind == cxx::Decl::Function ? tdecl->declarator : (tdecl->declarators.empty() ? 0 : tdecl->declarators[0].decl)) : 0;
        if (!tdcl || !tdcl->isFunction()) continue;
        const cxx::DeclaratorPart* fp = tdcl->functionPart();
        if (fp->params.size() != ft->params.size()) continue;
        Scope* tscope = tmpl->templateParams.empty() ? 0 : tmpl->templateParams[0]->scope;
        std::map<std::string, const Type*> bind; std::map<std::string, long> ints;
        const std::set<std::string> savedFixed = m_fixedTemplateParams;
        m_fixedTemplateParams.clear();
        bool ok = true;
        if (dcl->name.parts.back().hasTemplateArgs) {
            std::vector<const Type*> etypes; std::vector<long> eints; std::string etext;
            if (dcl->name.parts.back().templateArgs.size() > tmpl->templateParams.size() || !templateArgText(dcl->name.parts.back().templateArgs, in, etypes, eints, etext)) ok = false;
            for (size_t i = 0; ok && i < etypes.size(); ++i) {
                const std::string& pn = tmpl->templateParams[i]->name;
                if (etypes[i]) bind[pn] = etypes[i]; else ints[pn] = eints[i];
                m_fixedTemplateParams.insert(pn);
            }
        }
        for (size_t k = 0; ok && k < fp->params.size(); ++k) if (!deduce(fp->params[k], ft->params[k], tscope, bind, ints)) ok = false;
        m_fixedTemplateParams = savedFixed;
        std::string key = "<";
        for (size_t i = 0; ok && i < tmpl->templateParams.size(); ++i) {
            const std::string& pn = tmpl->templateParams[i]->name;
            if (i) key += ", ";
            if (bind.count(pn)) key += bind[pn]->text();
            else if (ints.count(pn)) { std::ostringstream vs; vs << ints[pn]; key += vs.str(); }
            else ok = false;
        }
        if (!ok) continue;
        key += ">";
        Scope* iscope = m_prog.newScope(Scope::Instantiation, in, tmpl);
        for (size_t i = 0; i < tmpl->templateParams.size(); ++i) {
            const std::string& pn = tmpl->templateParams[i]->name;
            Symbol* b = m_prog.newSymbol(bind.count(pn) ? Symbol::Typedef : Symbol::Enumerator, pn, iscope);
            if (bind.count(pn)) b->type = bind[pn]; else { b->type = m_types.basic(Type::Int); b->hasConstValue = true; b->constValue = ints[pn]; }
        }
        dcl->name.parts.back().ident = name + key;
        dcl->name.parts.back().hasTemplateArgs = false;
        dcl->name.parts.back().templateArgs.clear();
        Symbol* fn = declareFunction(inner, dcl, inner->spec, iscope, access, ft, params);
        if (!fn) return;
        fn->instantiatedFrom = tmpl;
        fn->isDependentContext = false;
        for (size_t i = 0; i < tmpl->templateParams.size(); ++i) {
            const std::string& pn = tmpl->templateParams[i]->name;
            fn->templateArgs.push_back(bind.count(pn) ? bind[pn] : 0);
            fn->templateIntArgs.push_back(ints.count(pn) ? ints[pn] : 0);
        }
        tmpl->instantiations[key] = fn;
        if (inner->kind == cxx::Decl::Function) {
            if (fn->hasBody && fn->definition && fn->definition != inner) { error(d->loc, "S110", "redefinition of '" + fn->qualifiedName() + "'"); return; }
            fn->hasBody = true; fn->definition = inner;
            PendingBody pb; pb.fn = fn; pb.def = inner; pb.scope = iscope; queueBody(pb);
        }
        d->semaSym = fn; inner->semaSym = fn;
        return;
    }
    error(d->loc, "S111", "no function template '" + name + "' matches the explicit specialisation");
}

bool Sema::derivesFrom(Symbol* derived, Symbol* base, int* offset, int depth)
{
    if (!derived || !base || depth > 32) return false;
    if (derived == base) { if (offset) *offset = 0; return true; }
    for (size_t i = 0; i < derived->bases.size(); ++i) {
        int o = 0;
        if (derivesFrom(derived->bases[i].cls, base, &o, depth + 1)) { if (offset) *offset = derived->bases[i].offset + o; return true; }
    }
    return false;
}

namespace {

// the paths from cls down to base that take no virtual edge (cls itself counts when it is base)
int nonVirtualPaths(const Symbol* cls, const Symbol* base, int depth)
{
    if (!cls || depth > 32) return 0;
    if (cls == base) return 1;
    int n = 0;
    for (size_t i = 0; i < cls->bases.size(); ++i) if (!cls->bases[i].isVirtual) n += nonVirtualPaths(cls->bases[i].cls, base, depth + 1);
    return n;
}

// every virtual base of cls, direct or inherited, once
void virtualBases(const Symbol* cls, std::vector<const Symbol*>& out, int depth)
{
    if (!cls || depth > 32) return;
    for (size_t i = 0; i < cls->bases.size(); ++i) {
        const Symbol* b = cls->bases[i].cls;
        if (cls->bases[i].isVirtual && std::find(out.begin(), out.end(), b) == out.end()) out.push_back(b);
        virtualBases(b, out, depth + 1);
    }
}

} // namespace

// how many subobjects of class 'base' an object of 'derived' holds: more than one makes a conversion to it or an
// unqualified use of its non-static members ambiguous (cl C2594, C2385); a virtual base counts once
int Sema::baseSubobjects(Symbol* derived, Symbol* base)
{
    int n = nonVirtualPaths(derived, base, 0);
    std::vector<const Symbol*> vb;
    virtualBases(derived, vb, 0);
    for (size_t i = 0; i < vb.size(); ++i) n += nonVirtualPaths(vb[i], base, 0);
    return n;
}

namespace {

// the alignment __declspec(align(N)) demands of a type, which #pragma pack cannot lower: declared on its class or
// inherited from an aligned member or base (Symbol::declAlign of a laid-out class)
int declaredFloor(const Type* t)
{
    while (t && t->kind == Type::Array) t = t->inner;
    return t && t->kind == Type::Class && t->decl && t->decl->layoutDone ? t->decl->declAlign : 0;
}

// MSVC's vbtables of a class whose virtual bases are placed: its own vbptr's (when it shares none) and those
// inherited from its bases, the one it shares extended with its new virtual bases. Tables that would share a name
// are named apart by the bases they come through, as cl (and clang's MicrosoftVTableContext) does
void layoutVbtables(Symbol* cls, int sharedVb)
{
    std::vector<VbTable> tables;
    if (sharedVb < 0) {
        VbTable t;
        t.offset = cls->vbptrOffset; t.introducedBy = cls; t.order = cls->vbases; t.nextToMangle = cls;
        tables.push_back(t);
    }
    // the vbptrs of the non-virtual bases' non-virtual parts, then those of every virtual base (once, at its
    // place in this class)
    std::vector<std::pair<Symbol*, int> > parts;
    std::vector<int> partIndex;
    for (size_t b = 0; b < cls->bases.size(); ++b)
        if (cls->bases[b].cls && !cls->bases[b].isVirtual) { parts.push_back(std::make_pair(cls->bases[b].cls, cls->bases[b].offset)); partIndex.push_back(static_cast<int>(b)); }
    for (size_t v = 0; v < cls->vbases.size(); ++v) { parts.push_back(std::make_pair(cls->vbases[v], cls->vbaseOffsets[v])); partIndex.push_back(-1); }
    for (size_t p = 0; p < parts.size(); ++p) {
        Symbol* bc = parts[p].first;
        for (size_t k = 0; k < bc->vbtables.size(); ++k) {
            if (bc->vbtables[k].offset >= bc->nvSize) continue;   // in its virtual part: placed by this class
            VbTable t = bc->vbtables[k];
            t.offset += parts[p].second;
            Symbol* via = bc;
            // a virtual base inherited through a direct base keeps the name that base gives its table
            for (size_t b = 0; partIndex[p] < 0 && via == bc && b < cls->bases.size(); ++b) {
                Symbol* d = cls->bases[b].cls;
                if (!d || d == bc) continue;
                const size_t v = static_cast<size_t>(std::find(d->vbases.begin(), d->vbases.end(), bc) - d->vbases.begin());
                if (v >= d->vbases.size()) continue;
                for (size_t u = 0; u < d->vbtables.size(); ++u)
                    if (d->vbtables[u].offset == d->vbaseOffsets[v] + bc->vbtables[k].offset) { t.path = d->vbtables[u].path; t.nextToMangle = d->vbtables[u].nextToMangle; via = d; }
            }
            if (t.path.empty() || t.path.back() != via) t.nextToMangle = via;
            if (sharedVb >= 0 && partIndex[p] == sharedVb && bc->vbtables[k].offset == bc->vbptrOffset)
                for (size_t v = 0; v < cls->vbases.size(); ++v) if (std::find(t.order.begin(), t.order.end(), cls->vbases[v]) == t.order.end()) t.order.push_back(cls->vbases[v]);
            tables.push_back(t);
        }
    }
    for (size_t t = 0; t < tables.size(); ++t) {
        VbTable& vt = tables[t];
        const int self = vt.entries.empty() ? -vt.offset : vt.entries[0];
        vt.entries.assign(1, self);
        for (size_t k = 0; k < vt.order.size(); ++k) {
            const size_t v = static_cast<size_t>(std::find(cls->vbases.begin(), cls->vbases.end(), vt.order[k]) - cls->vbases.begin());
            vt.entries.push_back(v < cls->vbaseOffsets.size() ? cls->vbaseOffsets[v] - vt.offset : 0);
        }
    }
    for (int round = 0; round < 32; ++round) {
        bool moved = false;
        std::vector<bool> clash(tables.size(), false);
        for (size_t t = 0; t < tables.size(); ++t)
            for (size_t u = 0; u < tables.size(); ++u) if (u != t && tables[u].path == tables[t].path) clash[t] = true;
        for (size_t t = 0; t < tables.size(); ++t)
            if (clash[t] && tables[t].nextToMangle && (tables[t].path.empty() || tables[t].path.back() != tables[t].nextToMangle)) { tables[t].path.push_back(tables[t].nextToMangle); moved = true; }
        if (!moved) break;
    }
    cls->vbtables = tables;
}

} // namespace

void Sema::layoutClass(Symbol* cls)
{
    if (cls->layoutDone) return;
    // #pragma pack(n) caps the alignment of every member at n; __declspec(align(N)) is a floor it cannot lower
    const int pack = cls->classAst ? cls->classAst->pack : 0;
    int floor = cls->classAst ? cls->classAst->declAlign : 0;
    int off = 0, align = 1;
    bool vptr = false;
    // MSVC's empty bases: a base takes its size without the byte an empty class is given (nvSize), so an empty
    // base takes no space; a byte of padding separates a base that ends with a zero-sized subobject from a
    // following base that leads with one (E, E2 { int w; }: E at 0, E2 at 1, w at 4)
    // MSVC places the bases with a vfptr at offset 0 first, in declaration order, then the others: the first of them
    // (the primary base) is at offset 0 and the class shares its vptr (NP, PB { virtual }: PB at 0, NP after it)
    std::vector<size_t> baseOrder;
    for (size_t pass = 0; pass < 2; ++pass)
        for (size_t b = 0; b < cls->bases.size(); ++b) {
            Symbol* bc = cls->bases[b].cls;
            if (!bc) continue;
            if (!bc->layoutDone) layoutClass(bc);
            if (cls->bases[b].isVirtual) continue;           // placed after the non-virtual part
            if ((bc->vptrOffset == 0) == (pass == 0)) baseOrder.push_back(b);
        }
    const Symbol* prev = 0;
    bool leadsZero = false, endsZero = false;
    for (size_t k = 0; k < baseOrder.size(); ++k) {
        const size_t b = baseOrder[k];
        Symbol* bc = cls->bases[b].cls;
        if (k == 0 && bc->vptrOffset == 0) vptr = true;      // share the primary base's vptr
        int a = pack > 0 && bc->align > pack ? pack : bc->align;
        if (bc->declAlign > a) a = bc->declAlign;
        if (bc->declAlign > floor) floor = bc->declAlign;
        if (prev && prev->endsWithZeroSized && bc->leadsWithZeroSized) ++off;
        if (!prev) leadsZero = bc->leadsWithZeroSized;
        off = (off + a - 1) / a * a;
        cls->bases[b].offset = off;
        off += bc->nvSize;
        endsZero = bc->endsWithZeroSized;
        prev = bc;
        if (a > align) align = a;
    }
    // MSVC's virtual bases: every one, direct or inherited, in cl's order (a base's own virtual bases before it).
    // The class shares the vbptr of its first non-virtual base that has one, or gets its own after the
    // non-virtual bases (injected once the members are placed, as cl does: VN : NB, virtual VB { short n; }:
    // NB at 0, vbptr at 8, n at 18)
    std::vector<Symbol*> vbases;
    int sharedVb = -1;
    for (size_t b = 0; b < cls->bases.size(); ++b) {
        Symbol* bc = cls->bases[b].cls;
        if (!bc) continue;
        for (size_t v = 0; v < bc->vbases.size(); ++v) if (std::find(vbases.begin(), vbases.end(), bc->vbases[v]) == vbases.end()) vbases.push_back(bc->vbases[v]);
        if (cls->bases[b].isVirtual) { if (std::find(vbases.begin(), vbases.end(), bc) == vbases.end()) vbases.push_back(bc); }
        else if (sharedVb < 0 && bc->vbptrOffset >= 0) sharedVb = static_cast<int>(b);
    }
    // a vfptr of its own only for new virtual functions (one that overrides a function of a virtual base goes into
    // that base's vftable, which stays in the virtual base subobject: VL : virtual V { int f(); } has no vfptr of
    // its own, VN : virtual V { virtual int h(); } has one at 0)
    bool ownVirtuals = false, newVirtuals = false;
    for (size_t i = 0; i < cls->members->order.size(); ++i) {
        Symbol* m = cls->members->order[i];
        if (m->kind != Symbol::Function || !m->isVirtual) continue;
        ownVirtuals = true;
        if (!overridesAny(m, cls)) newVirtuals = true;
    }
    for (size_t b = 0; b < cls->bases.size() && !ownVirtuals; ++b) if (cls->bases[b].cls && cls->bases[b].cls->isPolymorphic) ownVirtuals = true;
    cls->isPolymorphic = ownVirtuals;
    if (newVirtuals && !vptr) {
        // a new vptr at offset 0: shift the bases
        for (size_t b = 0; b < cls->bases.size(); ++b) cls->bases[b].offset += 8;
        off += 8;
        cls->vptrOffset = 0;
        const int va = pack > 0 && pack < 8 ? pack : 8;
        if (align < va) align = va;
    } else if (vptr) cls->vptrOffset = 0;
    const int baseEnd = off;
    // cl's vtordisp for a polymorphic virtual base: a class with a user-declared constructor or destructor that
    // overrides one of the base's virtual functions (not only its destructor) gets the field, and so does every
    // class derived from one that has it (VL : virtual V { VL(); int f(); } and VL2 : VL { VL2(); }, not VO3 : VO
    // { VO3(); } with VO { int f(); } without a constructor)
    std::vector<bool> vtordisps(vbases.size(), false);
    for (size_t v = 0; v < vbases.size(); ++v) {
        Symbol* vb = vbases[v];
        if (!vb->isPolymorphic) continue;
        bool need = false;
        for (size_t b = 0; b < cls->bases.size() && !need; ++b) {
            Symbol* bc = cls->bases[b].cls;
            if (!bc) continue;
            for (size_t k = 0; k < bc->vbases.size() && k < bc->vtordisp.size(); ++k) if (bc->vbases[k] == vb && bc->vtordisp[k]) need = true;
        }
        if (!need && (cls->hasUserCtor || cls->hasCopyCtor || cls->hasDtor))
            for (size_t i = 0; i < cls->members->order.size() && !need; ++i) if (overridesFunctionOf(cls->members->order[i], vb)) need = true;
        vtordisps[v] = need;
    }
    // the members in declaration order, unnamed bit-fields included
    std::vector<Symbol*> members;
    for (size_t f = 0, u = 0; f <= cls->fields.size(); ++f) {
        for (; u < cls->unnamedBitFields.size() && cls->unnamedBitFields[u]->index <= static_cast<int>(f); ++u) members.push_back(cls->unnamedBitFields[u]);
        if (f < cls->fields.size()) members.push_back(cls->fields[f]);
    }
    // MSVC's bit-fields: a bit-field takes a unit of its declared type's size, at that type's alignment; the
    // next one shares the unit, from the low bit up, when its type has the same size and its bits fit. A
    // zero-width bit-field ends the unit and aligns to its type, and is ignored unless it follows a bit-field.
    // In a union, bit-fields are at offset 0 and do not raise the alignment.
    bool inUnit = false;                 // the previous member is a bit-field of width > 0
    int unitSize = 0, bitsLeft = 0;
    for (size_t f = 0; f < members.size(); ++f) {
        Symbol* fld = members[f];
        const Type* ft = fld->type;
        const Type* elem = ft;
        while (elem && elem->kind == Type::Array) elem = elem->inner;
        if (elem && elem->kind == Type::Class && elem->decl && !elem->decl->layoutDone) layoutClass(elem->decl);
        if (elem && elem->kind == Type::Class && elem->decl) endsZero = elem->decl->endsWithZeroSized;   // a member of class type has its full size
        int s = m_types.sizeOf(ft), a = m_types.alignOf(ft);
        if (ft->kind == Type::Reference) { s = 8; a = 8; }
        if (pack > 0 && a > pack) a = pack;
        const int need = fld->declAlign > declaredFloor(ft) ? fld->declAlign : declaredFloor(ft);
        if (need > a) a = need;
        if (need > floor) floor = need;
        if (fld->bitWidth >= 0) {
            fld->bitOffset = 0;
            if (fld->bitWidth == 0) {
                if (!inUnit) { fld->offset = cls->isUnion ? 0 : off; continue; }
                inUnit = false;
                if (cls->isUnion) { fld->offset = 0; if (s > off) off = s; continue; }
                off = (off + a - 1) / a * a; fld->offset = off;
                if (a > align) align = a;
                continue;
            }
            if (!cls->isUnion && inUnit && unitSize == s && fld->bitWidth <= bitsLeft) {
                fld->offset = off - s;
                fld->bitOffset = 8 * s - bitsLeft;
                bitsLeft -= fld->bitWidth;
                continue;
            }
            inUnit = true; unitSize = s;
            if (cls->isUnion) { fld->offset = 0; if (s > off) off = s; continue; }
            off = (off + a - 1) / a * a; fld->offset = off; off += s;
            bitsLeft = 8 * s - fld->bitWidth;
            if (a > align) align = a;
            continue;
        }
        inUnit = false;
        if (cls->isUnion) { fld->offset = 0; if (s > off) off = s; }
        else { off = (off + a - 1) / a * a; fld->offset = off; off += s; }
        if (a > align) align = a;
    }
    if (!vbases.empty()) {
        if (sharedVb >= 0) cls->vbptrOffset = cls->bases[static_cast<size_t>(sharedVb)].offset + cls->bases[static_cast<size_t>(sharedVb)].cls->vbptrOffset;
        else {
            // cl's injection: the vbptr at the next pointer boundary after the non-virtual bases, the members
            // moved down by the distance to its end rounded up to the alignment so far
            const int pa = pack > 0 && pack < 8 ? pack : 8;
            cls->vbptrOffset = (baseEnd + pa - 1) / pa * pa;
            int shift = cls->vbptrOffset + 8 - baseEnd;
            if (align > 1) shift = (shift + align - 1) / align * align;
            for (size_t f = 0; f < members.size(); ++f) members[f]->offset += shift;
            off += shift;
            if (align < pa) align = pa;
        }
    }
    if (floor > align) align = floor;
    cls->nvSize = (off + align - 1) / align * align;
    cls->size = cls->nvSize;
    if (!vbases.empty()) {
        // each virtual base once, after the non-virtual part, in cl's order
        int voff = cls->nvSize;
        for (size_t v = 0; v < vbases.size(); ++v) {
            Symbol* vb = vbases[v];
            int a = pack > 0 && vb->align > pack ? pack : vb->align;
            if (vb->declAlign > a) a = vb->declAlign;
            if (a > align) align = a;
            if (vtordisps[v]) voff += 4;                     // the vtordisp just before the base (VL: at 20, V at 24)
            voff = (voff + a - 1) / a * a;
            cls->vbaseOffsets.push_back(voff);
            voff += vb->nvSize;
        }
        cls->vbases = vbases;
        cls->vtordisp = vtordisps;
        cls->size = (voff + align - 1) / align * align;
        for (size_t b = 0; b < cls->bases.size(); ++b)
            if (cls->bases[b].isVirtual && cls->bases[b].cls)
                cls->bases[b].offset = cls->vbaseOffsets[static_cast<size_t>(std::find(vbases.begin(), vbases.end(), cls->bases[b].cls) - vbases.begin())];
        layoutVbtables(cls, sharedVb);
    }
    if (cls->size == 0) {
        // an empty class: size 1 as a complete object, nothing as a base; it leads and ends with itself
        cls->size = 1;
        leadsZero = endsZero = true;
    }
    cls->leadsWithZeroSized = leadsZero;
    cls->endsWithZeroSized = endsZero;
    // an empty base left at the very end of the class sits on its last byte (D8, E {}: E at 7, size 8)
    for (size_t b = 0; b < cls->bases.size(); ++b)
        if (cls->bases[b].cls && !cls->bases[b].isVirtual && cls->bases[b].cls->nvSize == 0 && cls->bases[b].offset >= cls->size) cls->bases[b].offset = cls->size - 1;
    // the members of anonymous unions and structs, now that the unnamed members holding them are placed
    for (size_t i = 0; i < cls->members->order.size(); ++i) {
        Symbol* m = cls->members->order[i];
        if (m->kind == Symbol::Variable && m->anonHost && m->anonOf) { m->offset = m->anonHost->offset + m->anonOf->offset; m->bitOffset = m->anonOf->bitOffset; }
    }
    cls->align = align;
    cls->declAlign = floor;
    cls->layoutDone = true;
}

void Sema::completeClass(Symbol* cls)
{
    if (!cls->members) return;
    // implicit special members
    const Type* ct = cls->type;
    if (!cls->hasUserCtor) {
        Symbol* c = m_prog.newSymbol(Symbol::Function, cls->ctorName(), cls->members);
        c->isConstructor = true; c->isImplicit = true; c->ownerClass = cls; c->type = m_types.function(m_types.basic(Type::Void), std::vector<const Type*>(), false, false); c->loc = cls->loc;
        cls->hasDefaultCtor = true;
    }
    if (!cls->hasCopyCtor) {
        std::vector<const Type*> ps; ps.push_back(m_types.reference(m_types.qualified(ct, true, false)));
        Symbol* c = m_prog.newSymbol(Symbol::Function, cls->ctorName(), cls->members);
        c->isConstructor = true; c->isImplicit = true; c->ownerClass = cls; c->type = m_types.function(m_types.basic(Type::Void), ps, false, false); c->loc = cls->loc; c->requiredParams = 1;
        Symbol* p = m_prog.newSymbol(Symbol::Variable, "other", 0); p->type = ps[0]; p->isParam = true; c->params.push_back(p);
    }
    if (!cls->hasCopyAssign) {
        std::vector<const Type*> ps; ps.push_back(m_types.reference(m_types.qualified(ct, true, false)));
        Symbol* c = m_prog.newSymbol(Symbol::Function, "operator=", cls->members);
        c->isOperator = true; c->isImplicit = true; c->ownerClass = cls; c->type = m_types.function(m_types.reference(ct), ps, false, false); c->loc = cls->loc; c->requiredParams = 1;
        Symbol* p = m_prog.newSymbol(Symbol::Variable, "other", 0); p->type = ps[0]; p->isParam = true; c->params.push_back(p);
        if (!cls->isDependentContext) {
            // 12.8/12: the implicit copy assignment is ill-formed (cl C2582) for a class with a reference member, a
            // const member, or a base or member whose own copy assignment cannot be generated; a use reports it
            for (size_t i = 0; i < cls->fields.size() && c->deletedWhy.empty(); ++i) {
                Symbol* f = cls->fields[i];
                if (!f->type || f->name.empty()) continue;
                if (f->type->kind == Type::Reference) { c->deletedWhy = "a data member '" + f->name + "' of reference type"; break; }
                const Type* ft = f->type;
                bool isConst = ft->isConst;
                while (ft->kind == Type::Array) { ft = ft->inner; if (ft->isConst) isConst = true; }
                if (isConst) { c->deletedWhy = "a const data member '" + f->name + "'"; break; }
                if (ft->kind == Type::Class && ft->decl && assignmentDeleted(ft->decl)) c->deletedWhy = "a data member '" + f->name + "' whose class cannot be assigned";
            }
            for (size_t b = 0; b < cls->bases.size() && c->deletedWhy.empty(); ++b)
                if (cls->bases[b].cls && assignmentDeleted(cls->bases[b].cls)) c->deletedWhy = "a base class '" + cls->bases[b].cls->name + "' that cannot be assigned";
        }
    }
    if (!cls->hasDtor) {
        Symbol* c = m_prog.newSymbol(Symbol::Function, "~" + cls->ctorName(), cls->members);
        c->isDestructor = true; c->isImplicit = true; c->ownerClass = cls; c->type = m_types.function(m_types.basic(Type::Void), std::vector<const Type*>(), false, false); c->loc = cls->loc;
        for (size_t b = 0; b < cls->bases.size(); ++b) { Symbol* bc = cls->bases[b].cls; if (!bc) continue; std::vector<Symbol*> out; lookupMember(bc, "~" + bc->ctorName(), out); for (size_t k = 0; k < out.size(); ++k) if (out[k]->isVirtual) c->isVirtual = true; }
    }
    if (cls->isDependentContext) return;
    layoutClass(cls);
    // vtable: the primary base's table first (the first non-virtual base with a vfptr at offset 0), then overrides
    // and new virtuals; a virtual base is never primary (its vfptr stays in the virtual base subobject)
    size_t primary = cls->bases.size();
    for (size_t b = 0; b < cls->bases.size(); ++b) if (cls->bases[b].cls && !cls->bases[b].isVirtual && cls->bases[b].cls->vptrOffset == 0) { primary = b; break; }
    if (primary < cls->bases.size()) cls->vtable = cls->bases[primary].cls->vtable;
    // MSVC's secondary vftables: every vfptr of the bases other than the one at offset 0 (those of the other
    // polymorphic bases, and the primary base's own secondary ones) keeps a table of its own at its offset here,
    // which starts as the base's and takes this class's overriders, through adjustor thunks where needed
    std::vector<VfTable> tables(1);
    tables[0].introducedBy = cls;
    if (primary < cls->bases.size() && !cls->bases[primary].cls->vftables.empty()) {
        tables[0].introducedBy = cls->bases[primary].cls->vftables[0].introducedBy;
        tables[0].thunks = cls->bases[primary].cls->vftables[0].thunks;
        tables[0].returnOf = cls->bases[primary].cls->vftables[0].returnOf;
        tables[0].returnAdjust = cls->bases[primary].cls->vftables[0].returnAdjust;
    }
    tables[0].thunks.resize(cls->vtable.size(), 0);
    tables[0].returnOf.resize(cls->vtable.size(), 0);
    tables[0].returnAdjust.resize(cls->vtable.size(), 0);
    for (size_t b = 0; b < cls->bases.size(); ++b) {
        Symbol* bc = cls->bases[b].cls;
        if (!bc || !bc->isPolymorphic || cls->bases[b].isVirtual) continue;
        for (size_t t = 0; t < bc->vftables.size(); ++t) {
            if (bc->vftables[t].vbase) continue;              // a table inside one of its virtual bases: this class's own copy below
            if (b == primary && bc->vftables[t].offset == 0) continue;
            tables.push_back(bc->vftables[t]);
            tables.back().offset += cls->bases[b].offset;
            tables.back().returnOf.resize(tables.back().fns.size(), 0);
            tables.back().returnAdjust.resize(tables.back().fns.size(), 0);
        }
    }
    // the vftables inside the polymorphic virtual bases, at their places in this class (cl: ??_7D@@6BV@@@): each
    // starts as the base's own tables, takes the overriders the direct bases hold for it, then this class's; the
    // thunks are recomputed below for this class's layout. Two direct bases overriding one function of the virtual
    // base differently leave no final overrider unless this class overrides it (10.3/2; cl C2250)
    std::vector<std::pair<std::pair<size_t, size_t>, std::pair<Symbol*, Symbol*> > > contested;   // (table, slot), the two overriders
    for (size_t v = 0; v < cls->vbases.size(); ++v) {
        Symbol* vb = cls->vbases[v];
        if (!vb->isPolymorphic) continue;
        for (size_t t = 0; t < vb->vftables.size(); ++t) {
            if (vb->vftables[t].vbase) continue;              // inside one of vb's own virtual bases: that one's own copy
            VfTable nt = vb->vftables[t];
            const int rel = nt.offset;
            nt.offset += cls->vbaseOffsets[v];
            nt.vbase = vb;
            nt.vtordisp = v < cls->vtordisp.size() && cls->vtordisp[v];
            nt.returnOf.resize(nt.fns.size(), 0);
            nt.returnAdjust.resize(nt.fns.size(), 0);
            for (size_t b = 0; b < cls->bases.size(); ++b) {
                Symbol* bc = cls->bases[b].cls;
                if (!bc || bc == vb) continue;
                size_t bv = 0;
                while (bv < bc->vbases.size() && bc->vbases[bv] != vb) ++bv;
                if (bv >= bc->vbases.size()) continue;
                for (size_t k = 0; k < bc->vftables.size(); ++k) {
                    const VfTable& bt = bc->vftables[k];
                    if (bt.vbase != vb || bt.offset - bc->vbaseOffsets[bv] != rel) continue;
                    for (size_t s = 0; s < bt.fns.size() && s < nt.fns.size(); ++s) {
                        if (bt.fns[s] == vb->vftables[t].fns[s]) continue;
                        if (nt.fns[s] == vb->vftables[t].fns[s]) {
                            nt.fns[s] = bt.fns[s];
                            if (s < bt.returnOf.size()) { nt.returnOf[s] = bt.returnOf[s]; nt.returnAdjust[s] = bt.returnAdjust[s]; }
                        } else if (bt.fns[s] != nt.fns[s]) contested.push_back(std::make_pair(std::make_pair(tables.size(), s), std::make_pair(nt.fns[s], bt.fns[s])));
                    }
                }
            }
            tables.push_back(nt);
        }
    }
    std::map<std::string, size_t> firstOfName;   // where each member function's name is first declared
    std::vector<std::pair<std::pair<size_t, size_t>, Symbol*> > fresh;   // new virtuals: (first of name, ~index)
    for (size_t i = 0; i < cls->members->order.size(); ++i) {
        Symbol* m = cls->members->order[i];
        if (m->kind != Symbol::Function || m->isStatic) continue;
        // cl groups overloads where the name is first declared, a non-virtual declaration included
        if (firstOfName.find(m->name) == firstOfName.end()) firstOfName[m->name] = i;
        // does it override a virtual function of a base? Every slot of the bases' tables is searched, not the
        // names lookup finds: a middle class may hide an overload it does not override (YB::f(double) over
        // YA::f(int), YA::f(double): YC::f(int) still overrides YA::f(int))
        Symbol* over = 0;
        for (size_t b = 0; b < cls->bases.size() && !over; ++b) {
            Symbol* bc = cls->bases[b].cls;
            if (!bc || !bc->isPolymorphic) continue;
            const size_t nt = bc->vftables.empty() ? 1 : bc->vftables.size();
            for (size_t t = 0; t < nt && !over; ++t) {
                const std::vector<Symbol*>& fns = bc->vftables.empty() ? bc->vtable : bc->vftables[t].fns;
                for (size_t k = 0; k < fns.size() && !over; ++k) {
                    Symbol* v = fns[k];
                    if (v && (m->isDestructor ? v->isDestructor : (!v->isDestructor && v->name == m->name && sameSignature(v->type, m->type)))) over = v;
                }
            }
        }
        if (over) { m->isVirtual = true; m->overrides = over; }
        if (!m->isVirtual) continue;
        // the slots m overrides, in the primary table (0) and the secondary ones; m is reached through the first of
        // them (lowest offset) and then expects this at that vfptr (MSVC's this adjustor)
        std::vector<std::pair<size_t, size_t> > hits;
        for (size_t s = 0; s < cls->vtable.size(); ++s) {
            Symbol* v = cls->vtable[s];
            if (v == over || (over && v->overrides && v->name == m->name && sameSignature(v->type, m->type)) || (m->isDestructor && v->isDestructor)) { hits.push_back(std::make_pair(static_cast<size_t>(0), s)); break; }
        }
        for (size_t t = 1; t < tables.size(); ++t)
            for (size_t s = 0; s < tables[t].fns.size(); ++s) {
                Symbol* v = tables[t].fns[s];
                if (m->isDestructor ? v->isDestructor : (!v->isDestructor && v->name == m->name && sameSignature(v->type, m->type))) hits.push_back(std::make_pair(t, s));
            }
        size_t first = hits.empty() ? 0 : hits[0].first;
        for (size_t h = 0; h < hits.size(); ++h) if (tables[hits[h].first].offset < tables[first].offset) first = hits[h].first;
        // cl's covariant returns: when the return class of m holds the return class of a function it overrides at a
        // non-zero offset, m gets a slot of its own at the end of the table that first holds the overridden function
        // (in the primary table among the class's new virtual functions, in declaration order) and every slot it
        // overrides holds a return-adjusting thunk (also where the adjustment is 0: E : CD { E* self(); } over CD's
        // slot); PD : SB { PD* self(); } with SB at 0 overrides in place
        std::vector<int> radj(hits.size(), 0);
        bool covariant = false;
        for (size_t h = 0; h < hits.size(); ++h) {
            const VfTable& v = tables[hits[h].first];
            const size_t s = hits[h].second;
            Symbol* root = v.returnOf[s] ? v.returnOf[s] : hits[h].first == 0 ? cls->vtable[s] : v.fns[s];   // (the primary table's fns are filled at the end)
            radj[h] = returnAdjustment(m, root);
            if (radj[h] != 0) covariant = true;
        }
        m->thisAdjust = m->vfptrOffset = 0;
        m->vfptrVbase = 0;
        bool placed = false;
        if (!covariant) {
            for (size_t h = 0; h < hits.size(); ++h)
                if (hits[h].first == 0) { cls->vtable[hits[h].second] = m; m->vtableSlot = static_cast<int>(hits[h].second); placed = true; tables[0].thunks[hits[h].second] = 0; }
            if (!placed && !hits.empty()) {
                for (size_t h = 0; h < hits.size(); ++h) if (hits[h].first == first) m->vtableSlot = static_cast<int>(hits[h].second);
                m->vfptrOffset = tables[first].offset;
                m->vfptrVbase = tables[first].vbase;
                // a destructor takes the complete object, unless its vfptr lies only in virtual bases: then, as cl,
                // it takes the first of them (VY::{dtor} this adjustor: 24)
                m->thisAdjust = m->isDestructor && !m->vfptrVbase ? 0 : m->vfptrOffset;
                placed = true;
            }
            for (size_t h = 0; h < hits.size(); ++h) {
                if (hits[h].first == 0) continue;
                VfTable& v = tables[hits[h].first];
                v.fns[hits[h].second] = m;
                v.thunks[hits[h].second] = v.offset - m->thisAdjust;
            }
        } else {
            if (first > 0) {
                VfTable& own = tables[first];
                m->vtableSlot = static_cast<int>(own.fns.size());
                m->vfptrOffset = m->thisAdjust = own.offset;
                m->vfptrVbase = own.vbase;
                own.fns.push_back(m); own.thunks.push_back(0); own.returnOf.push_back(0); own.returnAdjust.push_back(0);
                placed = true;
            }
            for (size_t h = 0; h < hits.size(); ++h) {
                VfTable& v = tables[hits[h].first];
                const size_t s = hits[h].second;
                Symbol* root = v.returnOf[s] ? v.returnOf[s] : hits[h].first == 0 ? cls->vtable[s] : v.fns[s];
                if (hits[h].first == 0) cls->vtable[s] = m; else v.fns[s] = m;
                v.thunks[s] = v.offset - m->thisAdjust;
                v.returnOf[s] = root;
                v.returnAdjust[s] = radj[h];
            }
        }
        if (!placed) fresh.push_back(std::make_pair(std::make_pair(firstOfName[m->name], ~i), m));
    }
    // the new virtual functions, as cl orders them: by declaration, but overloads of a name together where the
    // name is first declared (an overrider counts), the last declared first (a, f(int), b, f(double): a,
    // f(double), f(int), b)
    std::sort(fresh.begin(), fresh.end());
    for (size_t k = 0; k < fresh.size(); ++k) {
        Symbol* m = fresh[k].second;
        m->vtableSlot = static_cast<int>(cls->vtable.size());
        cls->vtable.push_back(m);
    }
    tables[0].fns = cls->vtable;
    tables[0].thunks.resize(cls->vtable.size(), 0);
    tables[0].returnOf.resize(cls->vtable.size(), 0);
    tables[0].returnAdjust.resize(cls->vtable.size(), 0);
    for (size_t k = 0; k < contested.size(); ++k) {
        const size_t t = contested[k].first.first, s = contested[k].first.second;
        Symbol* held = t < tables.size() && s < tables[t].fns.size() ? tables[t].fns[s] : 0;
        if (!held || held->ownerClass == cls) continue;   // overridden here: the final overrider
        const Symbol* a = contested[k].second.first;
        const Symbol* b = contested[k].second.second;
        error(cls->loc, "S145", "'" + cls->qualifiedName() + "': ambiguous inheritance of '" + (held->overrides ? held->overrides->qualifiedName() : held->qualifiedName()) + "', overridden by '" + a->qualifiedName() + "' and '" + b->qualifiedName() + "' (cl C2250)");
    }
    // the thunks of the tables inside the virtual bases, for this class's layout: from the base's place here to
    // where each entry expects this (its owner's subobject plus its own adjustor); an entry of the base itself
    // moves nothing
    for (size_t t = 1; t < tables.size(); ++t) {
        if (!tables[t].vbase) continue;
        for (size_t s = 0; s < tables[t].fns.size(); ++s) {
            Symbol* fn = tables[t].fns[s];
            int eo = 0;
            if (!fn || !completeOffset(cls, fn->ownerClass, eo)) continue;
            tables[t].thunks[s] = tables[t].offset - (eo + fn->thisAdjust);
            // an overrider that lies in another virtual base: its distance from this base is not fixed (cl's
            // vtordispex thunk reads the vbtable)
            int nv = 0;
            if (!nonVirtualOffset(cls, fn->ownerClass, nv, 0) && !nonVirtualOffset(tables[t].vbase, fn->ownerClass, nv, 0)) tables[t].vtordispex = fn;
        }
    }
    if (cls->vptrOffset != 0) tables.erase(tables.begin());   // no vfptr of its own: only the inherited tables
    if (cls->isPolymorphic) cls->vftables = tables;
    cls->isAbstract = false;
    for (size_t t = 0; t < cls->vftables.size(); ++t)
        for (size_t s = 0; s < cls->vftables[t].fns.size(); ++s) if (cls->vftables[t].fns[s]->isPureVirtual) cls->isAbstract = true;
}


const Type* Sema::instantiateClassTemplate(Symbol* tmpl, const std::vector<cxx::TemplateArg>& args, Scope* from, const cxx::Loc& loc)
{
    std::vector<const Type*> types; std::vector<long> ints; std::string key;
    if (!templateArgText(args, from, types, ints, key)) { error(loc, "S111", "bad template arguments for '" + tmpl->name + "'"); return m_types.error(); }
    for (size_t i = 0; i < types.size(); ++i) if (types[i] && types[i]->isDependent()) return m_types.dependent(tmpl);
    if (key.find('?') != std::string::npos) return m_types.dependent(tmpl);
    // defaults
    size_t nparams = tmpl->templateParams.size();
    if (types.size() < nparams && tmpl->templateAst) {
        for (size_t i = types.size(); i < nparams; ++i) {
            const cxx::TemplateParam* p = tmpl->templateAst->templateParams[i];
            Scope* tmp = m_prog.newScope(Scope::Instantiation, tmpl->scope, tmpl);
            for (size_t k = 0; k < i; ++k) { Symbol* b = m_prog.newSymbol(types[k] ? Symbol::Typedef : Symbol::Enumerator, tmpl->templateParams[k]->name, tmp); if (types[k]) b->type = types[k]; else { b->type = m_types.basic(Type::Int); b->hasConstValue = true; b->constValue = ints[k]; } }
            if (p->kind == cxx::TemplateParam::Type && p->defaultType) { const Type* t = applyDeclarator(typeOf(p->defaultType->spec, tmp), *p->defaultType->decl, tmp, 0); types.push_back(t); ints.push_back(0); key = key.substr(0, key.size() - 1) + (types.size() > 1 ? ", " : "") + t->text() + ">"; }
            else if (p->kind == cxx::TemplateParam::NonType && p->nonType && p->nonType->defaultValue) { long v; expr(p->nonType->defaultValue, tmp); if (!constantValue(p->nonType->defaultValue, v)) v = 0; types.push_back(0); ints.push_back(v); std::ostringstream s; s << v; key = key.substr(0, key.size() - 1) + (types.size() > 1 ? ", " : "") + s.str() + ">"; }
            else { error(loc, "S111", "too few template arguments for '" + tmpl->name + "'"); return m_types.error(); }
        }
    }
    if (types.size() != nparams) { error(loc, "S111", "wrong number of template arguments for '" + tmpl->name + "'"); return m_types.error(); }
    std::map<std::string, Symbol*>::iterator it = tmpl->instantiations.find(key);
    if (it != tmpl->instantiations.end()) return it->second->type;
    if (!tmpl->classAst || !tmpl->classAst->isDefinition) {
        // only declared: an incomplete class type
        Symbol* cls = m_prog.newSymbol(Symbol::Class, tmpl->name + key, tmpl->scope);
        cls->type = m_types.classType(cls); cls->instantiatedFrom = tmpl; cls->templateArgs = types; cls->templateIntArgs = ints; cls->loc = loc;
        tmpl->instantiations[key] = cls;
        return cls->type;
    }
    // choose a partial specialisation whose pattern matches
    Symbol* chosen = tmpl;
    std::map<std::string, const Type*> bind; std::map<std::string, long> ibind;
    for (size_t p = 0; p < tmpl->partialSpecs.size(); ++p) {
        Symbol* ps = tmpl->partialSpecs[p];
        if (ps->kind != Symbol::ClassTemplate || ps->patternArgs.size() != types.size()) continue;
        std::map<std::string, const Type*> b; std::map<std::string, long> ib;
        bool ok = true;
        for (size_t i = 0; i < types.size() && ok; ++i) {
            const cxx::TemplateArg& pa = ps->patternArgs[i];
            if (pa.type) {
                if (!types[i]) { ok = false; break; }
                // pattern "T", "T*", "const T&": deduce against the argument type
                cxx::Param tmp; tmp.spec = pa.type->spec; tmp.decl = pa.type->decl;
                Scope* tscope = ps->templateParams.empty() ? 0 : ps->templateParams[0]->scope;
                if (!deduce(&tmp, types[i], tscope, b, ib)) ok = false;
            } else if (pa.expr) {
                if (types[i]) { ok = false; break; }
                if (pa.expr->kind == cxx::Expr::Name && pa.expr->name.parts.size() == 1) { ib[pa.expr->name.parts[0].ident] = ints[i]; }
                else { long v; if (!constantValue(pa.expr, v) || v != ints[i]) ok = false; }
            }
        }
        if (ok) { chosen = ps; bind = b; ibind = ib; break; }
    }
    // the instantiation scope binds the parameters: a type to a typedef, a value to a constant, a template (the
    // argument of a template template parameter: a class type whose declaration is the template) to an alias
    if (m_instantiationDepth >= 1000) {
        if (!m_instantiationOverflow) error(loc, "S148", "template instantiation depth exceeds 1000 instantiating '" + tmpl->name + key + "' (cl C2999)");
        m_instantiationOverflow = true;
        return m_types.error();
    }
    Scope* iscope = m_prog.newScope(Scope::Instantiation, chosen->scope->kind == Scope::TemplateParams ? chosen->scope->parent : chosen->scope, chosen);
    if (chosen == tmpl) {
        for (size_t i = 0; i < nparams; ++i) {
            if (types[i] && types[i]->kind == Type::Class && types[i]->decl && types[i]->decl->kind == Symbol::ClassTemplate) {
                Symbol* b = m_prog.newSymbol(Symbol::ClassTemplate, tmpl->templateParams[i]->name, iscope);
                b->instantiatedFrom = types[i]->decl; b->isDependentContext = true;
                continue;
            }
            Symbol* b = m_prog.newSymbol(types[i] ? Symbol::Typedef : Symbol::Enumerator, tmpl->templateParams[i]->name, iscope);
            if (types[i]) b->type = types[i]; else { b->type = tmpl->templateParams[i]->type ? tmpl->templateParams[i]->type : m_types.basic(Type::Int); b->hasConstValue = true; b->constValue = ints[i]; }
        }
    } else {
        for (size_t i = 0; i < chosen->templateParams.size(); ++i) {
            const std::string& pn = chosen->templateParams[i]->name;
            if (bind.count(pn) && bind[pn]->kind == Type::Class && bind[pn]->decl && bind[pn]->decl->kind == Symbol::ClassTemplate) {
                Symbol* b = m_prog.newSymbol(Symbol::ClassTemplate, pn, iscope);
                b->instantiatedFrom = bind[pn]->decl; b->isDependentContext = true;
                continue;
            }
            Symbol* b = m_prog.newSymbol(bind.count(pn) ? Symbol::Typedef : Symbol::Enumerator, pn, iscope);
            if (bind.count(pn)) b->type = bind[pn]; else { b->type = m_types.basic(Type::Int); b->hasConstValue = true; b->constValue = ibind.count(pn) ? ibind[pn] : 0; }
        }
    }
    Symbol* cls = m_prog.newSymbol(Symbol::Class, tmpl->name + key, iscope);
    cls->type = m_types.classType(cls); cls->instantiatedFrom = tmpl; cls->templateArgs = types; cls->templateIntArgs = ints; cls->loc = loc;
    cls->isImplicitInstantiation = true;
    tmpl->instantiations[key] = cls;
    ++m_stats.classInstantiations;
    // the class template's name inside the instantiation means the instantiation (injected class name)
    Symbol* inj = m_prog.newSymbol(Symbol::Typedef, tmpl->name, iscope); inj->type = cls->type;
    cxx::ClassDecl* ast = cxx::cloneClassDecl(chosen->classAst, nodeArena());   // annotations belong to this instantiation
    Symbol* savedClass = m_currentClass;
    ++m_instantiationDepth;
    declareClass(ast, iscope, cxx::AccessPublic, cls);
    --m_instantiationDepth;
    cls->name = tmpl->name + key;
    m_currentClass = savedClass;
    // members defined out of line ("template <class T> void Stack<T>::push(...)")
    for (size_t p = 0; p < tmpl->partialSpecs.size(); ++p) {
        Symbol* mt = tmpl->partialSpecs[p];
        if (mt->kind != Symbol::FunctionTemplate || !mt->declAst) continue;
        Scope* mscope = m_prog.newScope(Scope::Instantiation, iscope, cls);
        for (size_t i = 0; i < nparams && i < mt->templateParams.size(); ++i) {
            Symbol* b = m_prog.newSymbol(types[i] ? Symbol::Typedef : Symbol::Enumerator, mt->templateParams[i]->name, mscope);
            if (types[i]) b->type = types[i]; else { b->type = m_types.basic(Type::Int); b->hasConstValue = true; b->constValue = ints[i]; }
        }
        Symbol* inj2 = m_prog.newSymbol(Symbol::Typedef, tmpl->name, mscope); inj2->type = cls->type;
        cxx::Decl* def = mt->declAst;
        if (def->kind == cxx::Decl::Function && def->declarator) {
            // a copy of the definition for this instantiation, with "Stack<T>::push" rewritten to the
            // instantiation's name so that the member is found
            cxx::Decl* copy = cxx::cloneDecl(def, nodeArena());
            cxx::Declarator* dcopy = copy->declarator;
            if (dcopy->name.parts.size() >= 2) { dcopy->name.parts[dcopy->name.parts.size() - 2].ident = cls->name; dcopy->name.parts[dcopy->name.parts.size() - 2].hasTemplateArgs = false; }
            declare(copy, mscope, cxx::AccessPublic);
        }
    }
    return cls->type;
}


namespace {

void dumpScope(const Scope* s, int depth, std::ostringstream& out)
{
    std::string pad(static_cast<size_t>(depth * 2), ' ');
    if (depth > 40) { out << pad << "...\n"; return; }
    for (size_t i = 0; i < s->order.size(); ++i) {
        const Symbol* y = s->order[i];
        switch (y->kind) {
        case Symbol::Namespace: out << pad << "namespace " << (y->name.empty() ? "(anonymous)" : y->name) << "\n"; if (y->members && y->members->owner == y) dumpScope(y->members, depth + 1, out); break;
        case Symbol::Class:
            out << pad << (y->isStruct ? "struct " : "class ") << y->name;
            for (size_t b = 0; b < y->bases.size(); ++b) out << (b ? ", " : " : ") << (y->bases[b].cls ? y->bases[b].cls->name : std::string("?")) << "@" << y->bases[b].offset;
            if (y->layoutDone) out << "  size " << y->size << " align " << y->align << (y->isPolymorphic ? " vptr" : "") << (y->isAbstract ? " abstract" : "");
            if (y->layoutDone && y->vbptrOffset >= 0) {
                out << " vbptr " << y->vbptrOffset << " vbases";
                for (size_t v = 0; v < y->vbases.size(); ++v) out << " " << y->vbases[v]->name << "@" << y->vbaseOffsets[v];
                for (size_t t = 0; t < y->vbtables.size(); ++t) {
                    out << " vbtable";
                    for (size_t p = 0; p < y->vbtables[t].path.size(); ++p) out << (p ? "/" : "@") << y->vbtables[t].path[p]->name;
                    out << "@" << y->vbtables[t].offset << ":";
                    for (size_t e = 0; e < y->vbtables[t].entries.size(); ++e) out << (e ? "," : "") << y->vbtables[t].entries[e];
                }
            }
            out << "\n";
            if (y->members) dumpScope(y->members, depth + 1, out);
            break;
        case Symbol::Enum: out << pad << "enum " << y->name << "\n"; break;
        case Symbol::Typedef: out << pad << "typedef " << (y->type ? y->type->text() : std::string("?")) << " " << y->name << "\n"; break;
        case Symbol::Variable:
            out << pad << (y->isStatic ? "static " : "") << (y->type ? y->type->text() : std::string("?")) << " " << y->name;
            if (y->isMember && !y->isStatic) out << "  @" << y->offset;
            if (y->hasConstValue) out << " = " << y->constValue;
            out << "\n";
            break;
        case Symbol::Function:
            if (y->isImplicit) break;
            out << pad << (y->isVirtual ? "virtual " : "") << (y->isStatic ? "static " : "") << (y->type ? y->type->text() : std::string("?")) << " " << y->name;
            if (y->vtableSlot >= 0) out << "  slot " << y->vtableSlot;
            out << (y->hasBody ? "  {body}" : "") << "\n";
            break;
        case Symbol::Enumerator: out << pad << "enumerator " << y->name << " = " << y->constValue << "\n"; break;
        case Symbol::ClassTemplate: {
            out << pad << "template class " << y->name << "  (" << y->instantiations.size() << " instantiation(s))\n";
            for (std::map<std::string, Symbol*>::const_iterator it = y->instantiations.begin(); it != y->instantiations.end(); ++it) {
                const Symbol* c = it->second;
                out << pad << "  instantiation " << c->name << (c->layoutDone ? "  size " : "") ; if (c->layoutDone) out << c->size; out << "\n";
                if (c->members) dumpScope(c->members, depth + 2, out);
            }
            break;
        }
        case Symbol::FunctionTemplate: {
            out << pad << "template function " << y->name << "  (" << y->instantiations.size() << " instantiation(s))\n";
            for (std::map<std::string, Symbol*>::const_iterator it = y->instantiations.begin(); it != y->instantiations.end(); ++it)
                out << pad << "  instantiation " << it->second->name << " " << (it->second->type ? it->second->type->text() : std::string("")) << "\n";
            break;
        }
        default: break;
        }
    }
}

} // namespace

std::string Sema::dumpSymbols() const
{
    std::ostringstream out;
    dumpScope(m_prog.global, 0, out);
    return out.str();
}

} // namespace sema
} // namespace cppparse
