//
//  Semantic.cpp
//  Compiler++
//
//  Created by G. R. Akhtar on 29/08/2026.
//
//  C++98 only.  See Semantic.h for how this pass spans both class layers.

#include "Semantic.h"

// The native table: a bodyless declaration of one of its names is a binding
// to the machine, so the analyser has to be able to ask what it is binding to.
#include "Bytecode.h"

#include <cmath>
#include <cstddef>
#include <sstream>

SemanticAnalyzer::SemanticAnalyzer(Diagnostics &d)
    : diag(d), currentReturnType(0), currentFunction(0), currentIsCtorOrDtor(false),
      currentMethodIsConst(false), loopDepth(0),
      switchDepth(0) {}

SemanticAnalyzer::~SemanticAnalyzer() {
    for (std::size_t i = 0; i < ownedTypes.size(); ++i) delete ownedTypes[i];
}

void SemanticAnalyzer::error(cc::ASTNode *at, const std::string &msg) {
    if (at) diag.error(at->line, at->col, msg);
    else    diag.error(0, 0, msg);
}

cc::Type *SemanticAnalyzer::makeBuiltin(cc::BuiltinKind k) {
    cc::Type *t = new cc::BuiltinType(k);
    ownedTypes.push_back(t);
    return t;
}

// Can this value answer "is it true?"  Anything arithmetic, any pointer, and
// bool itself.
bool SemanticAnalyzer::isTestable(cc::Type *t) {
    if (!t) return true;                    // an earlier error already spoke
    cc::BuiltinKind k;
    if (arithmeticKind(t, k)) return true;
    return dynamic_cast<cc::PointerType*>(stripReference(decay(t))) != 0;
}

bool SemanticAnalyzer::isBoolType(cc::Type *t) {
    return dynamic_cast<cxx::BoolType*>(stripReference(t)) != 0;
}

cc::Type *SemanticAnalyzer::makeBool() {
    cc::Type *t = new cxx::BoolType();
    ownedTypes.push_back(t);
    return t;
}

bool SemanticAnalyzer::arithmeticKind(cc::Type *t, cc::BuiltinKind &out) {
    if (isBoolType(t)) { out = cc::BK_Int; return true; }
    cc::BuiltinKind k;
    if (!builtinKindOf(t, k) || !cc::builtinIsArithmetic(k)) return false;
    out = k;
    return true;
}

bool SemanticAnalyzer::builtinKindOf(cc::Type *t, cc::BuiltinKind &out) {
    cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(stripReference(t));
    if (!bt) return false;
    out = bt->kind;
    return true;
}

// Anything narrower than int is computed as an int.  This is why `char + char`
// has type int in C++, and why a compiler needs a rank rather than a size.
cc::BuiltinKind SemanticAnalyzer::promote(cc::BuiltinKind k) {
    if (cc::builtinIsFloating(k)) return k;
    return cc::builtinRank(k) < cc::builtinRank(cc::BK_Int) ? cc::BK_Int : k;
}

// The common type two operands meet in: floating wins over integer, then the
// higher rank wins, and at equal rank unsigned wins over signed.
cc::BuiltinKind SemanticAnalyzer::usualArithmetic(cc::BuiltinKind a, cc::BuiltinKind b) {
    if (a == cc::BK_Double || b == cc::BK_Double) return cc::BK_Double;
    if (a == cc::BK_Float  || b == cc::BK_Float)  return cc::BK_Float;
    a = promote(a);
    b = promote(b);
    if (a == b) return a;
    const int ra = cc::builtinRank(a), rb = cc::builtinRank(b);
    if (ra != rb) return (ra > rb) ? a : b;
    return cc::builtinIsSigned(a) ? b : a;      // equal rank: unsigned wins
}

// The range of an integer kind, as the smallest and largest value it holds.
static void integerRange(cc::BuiltinKind k, double &lo, double &hi) {
    const int bits = cc::builtinSize(k) * 8;
    if (cc::builtinIsSigned(k)) {
        const double half = std::pow(2.0, bits - 1);
        lo = -half;
        hi = half - 1;
    } else {
        lo = 0;
        hi = std::pow(2.0, bits) - 1;
    }
}

bool SemanticAnalyzer::literalFitsIn(cc::Expr *e, cc::BuiltinKind to) {
    // -5 is a literal too: the minus is a unary operator in the grammar, which
    // is no reason to warn that char cannot hold it.
    if (cc::UnaryExpr *u = dynamic_cast<cc::UnaryExpr*>(e)) {
        if (u->op == cc::UN_Neg) {
            if (cc::NumberExpr *nn = dynamic_cast<cc::NumberExpr*>(u->operand)) {
                if (cc::builtinIsFloating(to)) return true;
                double lo, hi;
                integerRange(to, lo, hi);
                const double v = -static_cast<double>(nn->value);
                return v >= lo && v <= hi;
            }
            if (dynamic_cast<cc::FloatExpr*>(u->operand)) {
                return literalFitsIn(u->operand, to);
            }
        }
    }
    if (cc::NumberExpr *n = dynamic_cast<cc::NumberExpr*>(e)) {
        if (cc::builtinIsFloating(to)) return true;
        double lo, hi;
        integerRange(to, lo, hi);
        const double v = static_cast<double>(n->value);
        return v >= lo && v <= hi;
    }
    if (cc::FloatExpr *f = dynamic_cast<cc::FloatExpr*>(e)) {
        if (to == cc::BK_Double) return true;
        // Exactly representable as a float?  Then the conversion loses nothing.
        if (to == cc::BK_Float) {
            return static_cast<double>(static_cast<float>(f->value)) == f->value;
        }
        return false;                       // a float literal into an integer
    }
    return false;
}

void SemanticAnalyzer::warnIfNarrowing(cc::Expr *e, cc::Type *from, cc::Type *to,
                                       cc::ASTNode *at, const std::string &what) {
    // Converting to bool is a normalisation, not a loss, and bool converts out
    // as 0 or 1, which fits everywhere.  Neither direction is narrowing.
    if (isBoolType(from) || isBoolType(to)) return;
    cc::BuiltinKind kf, kt;
    if (!builtinKindOf(from, kf) || !builtinKindOf(to, kt)) return;
    if (!cc::builtinIsArithmetic(kf) || !cc::builtinIsArithmetic(kt)) return;
    if (!isNarrowing(kf, kt)) return;
    if (e && literalFitsIn(e, kt)) return;
    diag.warning(at->line, at->col,
                 std::string("narrowing ") + describe(from) + " to " + describe(to)
                 + " in " + what);
}

// Legal, but worth saying out loud: the value may not survive the trip.
bool SemanticAnalyzer::isNarrowing(cc::BuiltinKind from, cc::BuiltinKind to) {
    if (from == to) return false;
    if (cc::builtinIsFloating(from) && !cc::builtinIsFloating(to)) return true;
    if (cc::builtinIsFloating(from) && cc::builtinIsFloating(to)) {
        return cc::builtinRank(to) < cc::builtinRank(from);
    }
    if (cc::builtinIsFloating(to)) return false;        // int -> float widens
    if (cc::builtinSize(to) < cc::builtinSize(from)) return true;
    // Same width but a different signedness reinterprets the top bit.
    if (cc::builtinSize(to) == cc::builtinSize(from) &&
        cc::builtinIsSigned(to) != cc::builtinIsSigned(from)) {
        return true;
    }
    return false;
}

// const travels with the copy: a clone that dropped it silently turned every
// const type back into a plain one.
cc::Type *SemanticAnalyzer::cloneType(cc::Type *t) {
    cc::Type *c = cloneTypeShape(t);
    if (c && t) c->isConst = t->isConst;
    return c;
}

cc::Type *SemanticAnalyzer::cloneTypeShape(cc::Type *t) {
    if (!t) return 0;
    cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(t);
    if (bt) return new cc::BuiltinType(bt->kind);
    if (dynamic_cast<cxx::BoolType*>(t)) return new cxx::BoolType();
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (ct) return new cxx::ClassType(ct->className);
    cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t);
    if (pt) return new cc::PointerType(cloneType(pt->base));
    cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t);
    if (at) return new cc::ArrayType(cloneType(at->element), at->count);
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t);
    if (rt) return new cxx::ReferenceType(cloneType(rt->base));
    return 0;
}

cc::Type *SemanticAnalyzer::makePointerTo(cc::Type *t) {
    cc::Type *p = new cc::PointerType(cloneType(t));
    ownedTypes.push_back(p);
    return p;
}

// --- Type helpers ---

// The one place arrays turn into pointers.  Everything that asks "what type
// does this expression have" goes through here; a declaration does not.
cc::Type *SemanticAnalyzer::decay(cc::Type *t) {
    cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(stripReference(t));
    if (!at) return t;
    cc::Type *p = new cc::PointerType(cloneType(at->element));
    ownedTypes.push_back(p);
    return p;
}

cc::Type *SemanticAnalyzer::stripReference(cc::Type *t) {
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t);
    return rt ? rt->base : t;
}

std::string SemanticAnalyzer::describe(cc::Type *t) {
    if (!t) return "<none>";
    // const reads before a value and after a star, exactly as it is written:
    // `const int*` is a pointer to const, `int* const` a const pointer.
    const std::string lead = t->isConst ? "const " : "";
    if (dynamic_cast<cxx::BoolType*>(t)) return lead + "bool";
    cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(t);
    if (bt) return lead + bt->name();
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (ct) return lead + ct->className;
    cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t);
    if (pt) return describe(pt->base) + "*" + (t->isConst ? " const" : "");
    cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t);
    if (at) {
        std::ostringstream ss;
        ss << describe(at->element) << "[" << at->count << "]";
        return ss.str();
    }
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t);
    if (rt) return describe(rt->base) + "&";
    return "<type>";
}

std::string SemanticAnalyzer::countText(std::size_t n) {
    std::ostringstream ss;
    ss << n;
    return ss.str();
}

// How a diagnostic names a parameter.  A named one is named, which is what
// three messages here always assumed -- they interpolated the name straight in
// and read "the argument to ''" when there was none.  A parameter need not
// have one: `void print_int(int);` is a complete declaration, and it heads a
// good part of this project's own test corpus.  So an unnamed parameter is
// given the two things that DO identify it, its position and its function.
std::string SemanticAnalyzer::parameterText(cc::Function *fn, std::size_t i) {
    if (fn && i < fn->params.size() && !fn->params[i]->name.empty())
        return "parameter '" + fn->params[i]->name + "'";
    std::ostringstream ss;
    ss << "parameter " << (i + 1) << " of '" << (fn ? fn->name : std::string("?")) << "'";
    return ss.str();
}

bool SemanticAnalyzer::isVoid(cc::Type *t) {
    cc::BuiltinKind k;
    return builtinKindOf(t, k) && k == cc::BK_Void;
}

bool SemanticAnalyzer::sameDeclaredType(cc::Type *a, cc::Type *b) {
    if (!a || !b) return a == b;
    if (a->isConst != b->isConst) return false;

    cxx::ReferenceType *ra = dynamic_cast<cxx::ReferenceType*>(a);
    cxx::ReferenceType *rb = dynamic_cast<cxx::ReferenceType*>(b);
    if (ra || rb) return ra && rb && sameDeclaredType(ra->base, rb->base);

    cc::PointerType *pa = dynamic_cast<cc::PointerType*>(a);
    cc::PointerType *pb = dynamic_cast<cc::PointerType*>(b);
    if (pa || pb) return pa && pb && sameDeclaredType(pa->base, pb->base);

    cc::ArrayType *aa = dynamic_cast<cc::ArrayType*>(a);
    cc::ArrayType *ab = dynamic_cast<cc::ArrayType*>(b);
    if (aa || ab) return aa && ab && sameDeclaredType(aa->element, ab->element);

    return sameType(a, b);
}

bool SemanticAnalyzer::sameType(cc::Type *a, cc::Type *b) {
    a = stripReference(a);
    b = stripReference(b);
    if (!a || !b) return true;          // an earlier error already spoke
    if (dynamic_cast<cxx::BoolType*>(a) || dynamic_cast<cxx::BoolType*>(b)) {
        return dynamic_cast<cxx::BoolType*>(a) && dynamic_cast<cxx::BoolType*>(b);
    }
    cc::BuiltinType *ba = dynamic_cast<cc::BuiltinType*>(a);
    cc::BuiltinType *bb = dynamic_cast<cc::BuiltinType*>(b);
    if (ba && bb) return ba->kind == bb->kind;
    cxx::ClassType *ca = dynamic_cast<cxx::ClassType*>(a);
    cxx::ClassType *cb = dynamic_cast<cxx::ClassType*>(b);
    if (ca && cb) return ca->className == cb->className;
    cc::PointerType *pa = dynamic_cast<cc::PointerType*>(a);
    cc::PointerType *pb = dynamic_cast<cc::PointerType*>(b);
    if (pa && pb) return sameType(pa->base, pb->base);
    return false;                       // different shapes entirely
}

cxx::ClassDecl *SemanticAnalyzer::classOf(cc::Type *t) {
    t = stripReference(t);
    cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t);
    if (pt) t = pt->base;
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    return ct ? findClass(ct->className) : 0;
}

// `T m` and `T m[2]` are both members made of T, and both are constructed --
// the second one element at a time.  classOf answers a different question and
// looks through a pointer to do it, which is exactly wrong here: a `T*` member
// is a value to copy, not an object to construct.
cxx::ClassDecl *SemanticAnalyzer::memberClassOf(cc::Type *t) {
    while (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) t = at->element;
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    return ct ? findClass(ct->className) : 0;
}

// A Derived object begins with its Base subobject at offset 0, so the upcast
// costs nothing.  The reverse is not allowed: a Base* need not be a Derived.
bool SemanticAnalyzer::canConvert(cc::Type *from, cc::Type *to) {
    if (sameType(from, to)) return true;
    if (!from || !to) return true;              // an earlier error already spoke

    cc::Type *f = stripReference(from);
    cc::Type *t = stripReference(to);

    // Any arithmetic type converts to any other, and bool is one of them: it
    // converts from anything as a test against zero, and to anything as 0 or 1.
    cc::BuiltinKind kf, kt;
    if (arithmeticKind(f, kf) && arithmeticKind(t, kt)) return true;
    // A pointer tests as a bool too:  if (p)
    if (isBoolType(t) && dynamic_cast<cc::PointerType*>(f)) return true;

    // Derived* -> Base*, and any object pointer -> void*
    cc::PointerType *pf = dynamic_cast<cc::PointerType*>(f);
    cc::PointerType *ptt = dynamic_cast<cc::PointerType*>(t);
    if (pf && ptt) {
        // void* is the generic address, as in C++.  Not the reverse: coming
        // back out needs a cast, because only the program knows what it is.
        if (isVoid(ptt->base)) return true;
        cxx::ClassDecl *df = classOf(pf->base);
        cxx::ClassDecl *dt = classOf(ptt->base);
        if (df && dt) return isDerivedFrom(df, dt);
        return false;
    }

    // Derived -> Base& , and Derived -> Base
    cxx::ClassDecl *cf = classOf(f);
    cxx::ClassDecl *ctd = classOf(t);
    if (cf && ctd) return isDerivedFrom(cf, ctd);

    return false;
}

// Its TYPE is int, so no type-only rule can accept it where a pointer is
// wanted -- the expression itself has to be looked at.
bool SemanticAnalyzer::isNullPointerConstant(cc::Expr *e) {
    cc::NumberExpr *n = dynamic_cast<cc::NumberExpr*>(e);
    return n && n->value == 0;
}

// Const may be ADDED on the way in, never taken away: a const int* may not
// become an int*, or every check above it could be sidestepped by one
// assignment.  The other direction is always safe.
bool SemanticAnalyzer::constQualificationOk(cc::Type *from, cc::Type *to) {
    cc::Type *f = stripReference(from);
    cc::Type *t = stripReference(to);
    for (;;) {
        cc::PointerType *pf = dynamic_cast<cc::PointerType*>(f);
        cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t);
        if (!pf || !pt) return true;            // not both pointers: nothing to compare
        f = pf->base;
        t = pt->base;
        if (!f || !t) return true;
        if (f->isConst && !t->isConst) return false;
    }
}

bool SemanticAnalyzer::convertible(cc::Expr *fromExpr, cc::Type *from, cc::Type *to) {
    if (dynamic_cast<cc::PointerType*>(stripReference(to)) && isNullPointerConstant(fromExpr)) {
        return true;                            // 0 is any pointer type
    }
    if (!constQualificationOk(from, to)) return false;
    // Binding a T& to a const object would lose the const just as surely.
    if (cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(to)) {
        if (rt->base && !rt->base->isConst && from && stripReference(from)->isConst) {
            return false;
        }
    }
    return canConvert(from, to);
}

// int, int*, int** need no resolution; a class name does.
bool SemanticAnalyzer::checkTypeIsKnown(cc::Type *t, cc::ASTNode *at, const std::string &where) {
    if (!t) return true;
    // A pointer or reference to a class needs only its NAME: `class A;` then
    // `A *p;` is the whole point of a forward declaration.  Anything by value
    // needs the definition, and says so below.
    cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t);
    if (pt) {
        if (dynamic_cast<cxx::ClassType*>(pt->base)) return true;
        return checkTypeIsKnown(pt->base, at, where);
    }
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t);
    if (rt) {
        if (dynamic_cast<cxx::ClassType*>(rt->base)) return true;
        return checkTypeIsKnown(rt->base, at, where);
    }
    cc::ArrayType *arr = dynamic_cast<cc::ArrayType*>(t);
    if (arr) return checkTypeIsKnown(arr->element, at, where);
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (ct && !findClass(ct->className)) {
        error(at, "unknown type '" + ct->className + "' in " + where);
        return false;
    }
    return true;
}

// --- Classes and their members ---

cxx::ClassDecl *SemanticAnalyzer::findClass(const std::string &name) {
    std::map<std::string, cxx::ClassDecl*>::iterator it = classes.find(name);
    return (it == classes.end()) ? 0 : it->second;
}

void SemanticAnalyzer::resolveBases() {
    std::map<std::string, cxx::ClassDecl*>::iterator it;
    for (it = classes.begin(); it != classes.end(); ++it) {
        cxx::ClassDecl *cd = it->second;
        if (cd->baseName.empty()) continue;
        cxx::ClassDecl *base = findClass(cd->baseName);
        if (!base) {
            error(cd, "unknown base class '" + cd->baseName + "' for class '" + cd->name + "'");
            continue;
        }
        if (base == cd) {
            error(cd, "class '" + cd->name + "' cannot inherit from itself");
            continue;
        }
        cd->base = base;
    }

    // A chain longer than the number of classes must have revisited one.
    for (it = classes.begin(); it != classes.end(); ++it) {
        cxx::ClassDecl *cd = it->second;
        std::size_t steps = 0;
        for (cxx::ClassDecl *p = cd->base; p; p = p->base) {
            if (p == cd || ++steps > classes.size()) {
                error(cd, "inheritance cycle involving class '" + cd->name + "'");
                cd->base = 0;               // break it, so later walks terminate
                break;
            }
        }
    }
}

// Most-derived first: the first match wins, which IS name hiding.
// The member an operator expression calls, if the left operand is an object
// that declares one.  A class without the operator is not an error here: the
// builtin rules below will say what is actually wrong with it.
// The object a method call is made on: `a.f()` uses a itself, `p->f()` uses
// what p points at.
bool SemanticAnalyzer::objectIsConst(cxx::MemberAccessExpr *ma) {
    if (!ma || !ma->base) return false;
    if (!ma->isArrow) return isConstExpr(ma->base);
    cc::Type *bt = stripReference(ma->base->resolvedType);
    cc::PointerType *pt = dynamic_cast<cc::PointerType*>(bt);
    return pt && pt->base && pt->base->isConst;
}

bool SemanticAnalyzer::isNonConstReferenceTo(cc::Type *t) {
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t);
    return rt && rt->base && !rt->base->isConst;
}

bool SemanticAnalyzer::isConstExpr(cc::Expr *e) {
    if (!e) return false;

    // Inside a const member function, *this is const -- so every field of it
    // is, whether written `x` or `this->x`.
    if (currentMethodIsConst) {
        if (cxx::MemberAccessExpr *tm = dynamic_cast<cxx::MemberAccessExpr*>(e)) {
            if (dynamic_cast<cxx::ThisExpr*>(tm->base)) return true;
        }
        if (cc::IdentExpr *id = dynamic_cast<cc::IdentExpr*>(e)) {
            Symbol *sym = symbols.lookup(id->name);
            if (sym && sym->kind == SYM_Field) return true;
        }
    }

    // A member is const when the object is, whatever the field says.
    if (cxx::MemberAccessExpr *ma = dynamic_cast<cxx::MemberAccessExpr*>(e)) {
        if (!ma->isArrow && isConstExpr(ma->base)) return true;
        // p->x through a const P*: the OBJECT is *p, so its constness is the
        // pointee's, not the pointer's.
        if (ma->isArrow && ma->base) {
            cc::Type *bt = stripReference(ma->base->resolvedType);
            cc::PointerType *pt = dynamic_cast<cc::PointerType*>(bt);
            if (pt && pt->base && pt->base->isConst) return true;
        }
    }
    // An element of a const ARRAY is const.  Through a POINTER it is not:
    // `char* const p` freezes p, not what p points at, so p[0] stays writable
    // and the pointee's own const decides.
    if (cc::IndexExpr *ix = dynamic_cast<cc::IndexExpr*>(e)) {
        // The one case that must NOT propagate is a const POINTER: `char* const
        // p` freezes p, not what p points at.  Everything else that is const --
        // a const array, a field of a const object, a field inside a const
        // member function -- passes its constness to the element.
        cc::Type *bt = ix->base ? stripReference(ix->base->resolvedType) : 0;
        const bool pointerItselfConst =
            bt && dynamic_cast<cc::PointerType*>(bt) != 0 && bt->isConst;
        if (!pointerItselfConst && isConstExpr(ix->base)) return true;
    }

    cc::Type *t = stripReference(e->resolvedType);
    return t && t->isConst;
}

bool SemanticAnalyzer::isClassType(cc::Type *t) {
    return dynamic_cast<cxx::ClassType*>(stripReference(t)) != 0;
}

// An operator expression looks for a function only when an operand is an
// object: overloading for two builtins is not a thing C++ allows either.
cc::Function *SemanticAnalyzer::findOperator(cc::Expr *lhs, cc::Type *lt,
                                             cc::Expr *rhs, cc::Type *rt,
                                             cc::BinaryOp op, cc::ASTNode *at) {
    if (!isClassType(lt) && !isClassType(rt)) return 0;

    // A member is preferred, and only exists when the object is on the left.
    if (cxx::MethodDecl *m = findMemberOperator(lt, op, rhs, rt, at)) return m;

    // operator= must be a member; C++ says so, and a non-member one could not
    // be generated for a class that never mentioned it.
    if (op == cc::BIN_Assign) return 0;

    return findFreeOperator(lhs, lt, rhs, rt, std::string("operator") + cc::binaryOpText(op));
}

// The unary minus of an object.  It is the one unary operator this compiler
// overloads, and it is the one worth overloading: a vector, a matrix or a
// complex number all have a negation, and none of them have a `!`.
//
// A MEMBER takes nothing at all -- `V operator-()` -- which is what makes it
// unary; the one-parameter `operator-` beside it is the binary subtraction,
// and the two are told apart here by nothing more than that count.  A
// NON-MEMBER takes the operand itself, which is how a class whose definition
// cannot be changed still gets a negation.
cc::Function *SemanticAnalyzer::findUnaryMinusOperator(cc::Expr *operand,
                                                       cc::Type *t,
                                                       cc::ASTNode *at) {
    if (!isClassType(t)) return 0;

    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(stripReference(t));
    cxx::ClassDecl *cd = ct ? findClass(ct->className) : 0;
    if (cd) {
        const std::vector<cc::Function*> cands = findMethods(cd, "operator-");
        for (std::size_t i = 0; i < cands.size(); ++i) {
            cxx::MethodDecl *m = dynamic_cast<cxx::MethodDecl*>(cands[i]);
            if (!m || !m->params.empty()) continue;
            cxx::ClassDecl *owner = findClass(m->ownerClass);
            if (!memberIsAccessible(m, owner)) {
                error(at, std::string("'operator-' is ") + cxx::accessText(memberAccess(m))
                          + " in class '" + (owner ? owner->name : ct->className) + "'");
            }
            checkConstUse(m, stripReference(t)->isConst, at);
            return m;
        }
    }

    std::map<std::string, std::vector<cc::Function*> >::const_iterator it =
        overloads.find("operator-");
    if (it == overloads.end()) return 0;
    const std::vector<cc::Function*> &free = it->second;
    cc::Function *exact = 0;
    cc::Function *viable = 0;
    for (std::size_t i = 0; i < free.size(); ++i) {
        cc::Function *f = free[i];
        if (f->params.size() != 1) continue;          // two is the binary one
        cc::Type *want = f->params[0]->type;
        if (exactForOverload(t, want))          { if (!exact)  exact  = f; continue; }
        if (convertible(operand, t, want))      { if (!viable) viable = f; }
    }
    return exact ? exact : viable;
}

// The file-scope operator whose two parameters accept these two operands.
cc::Function *SemanticAnalyzer::findFreeOperator(cc::Expr *lhs, cc::Type *lt,
                                                 cc::Expr *rhs, cc::Type *rt,
                                                 const std::string &name) {
    std::map<std::string, std::vector<cc::Function*> >::const_iterator it =
        overloads.find(name);
    if (it == overloads.end()) return 0;

    const std::vector<cc::Function*> &cands = it->second;
    cc::Function *exact = 0;
    cc::Function *viable = 0;
    for (std::size_t i = 0; i < cands.size(); ++i) {
        cc::Function *f = cands[i];
        if (f->params.size() != 2) continue;
        cc::Type *p0 = f->params[0]->type;
        cc::Type *p1 = f->params[1]->type;
        if (exactForOverload(lt, p0) && exactForOverload(rt, p1)) {
            if (!exact) exact = f;
            continue;
        }
        if (convertible(lhs, lt, p0) && convertible(rhs, rt, p1)) {
            if (!viable) viable = f;
        }
    }
    return exact ? exact : viable;
}

// An operator may be overloaded like any other member -- operator+(V) beside
// operator+(int) -- so the RIGHT operand chooses which.  Taking the first by
// name would reject `v + 5` for having the wrong argument type.
// obj(args) selects among the class's operator() overloads by the arguments,
// exactly as an ordinary call does.
cxx::MethodDecl *SemanticAnalyzer::findCallOperator(cc::Type *ot, cc::CallExpr *call) {
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(stripReference(ot));
    if (!ct) return 0;
    cxx::ClassDecl *cd = findClass(ct->className);
    if (!cd) return 0;
    const std::vector<cc::Function*> cands = findMethods(cd, "operator()");
    if (cands.empty()) return 0;
    cc::Function *chosen = resolveOverload(cands, call, "operator()");
    cxx::MethodDecl *m = dynamic_cast<cxx::MethodDecl*>(chosen);
    if (!m) return 0;
    cxx::ClassDecl *owner = findClass(m->ownerClass);
    if (!memberIsAccessible(m, owner)) {
        error(call, std::string("'operator()' is ") + cxx::accessText(memberAccess(m))
                    + " in class '" + (owner ? owner->name : cd->name) + "'");
    }
    checkConstUse(m, ot && stripReference(ot)->isConst, call);
    return m;
}

// operator[] takes one argument, so the index picks the overload.
cxx::MethodDecl *SemanticAnalyzer::findIndexOperator(cxx::ClassDecl *cd, cc::Expr *index,
                                                     cc::Type *it, bool objectConst,
                                                     cc::ASTNode *at) {
    if (!cd) return 0;
    const std::vector<cc::Function*> cands = findMethods(cd, "operator[]");
    // A container usually declares the pair -- `T& operator[](int)` beside
    // `T operator[](int) const` -- so constness picks between them before the
    // argument does.
    cxx::MethodDecl *exact = 0, *viable = 0;
    for (int pass = 0; pass < 2 && !exact && !viable; ++pass) {
        const bool wantConst = (pass == 0) ? objectConst : !objectConst;
        for (std::size_t i = 0; i < cands.size(); ++i) {
            cxx::MethodDecl *m = dynamic_cast<cxx::MethodDecl*>(cands[i]);
            if (!m || m->params.size() != 1) continue;
            if (m->isConstMethod != wantConst) continue;
            cc::Type *want = m->params[0]->type;
            if (it && exactForOverload(it, want)) { if (!exact) exact = m; continue; }
            if (convertible(index, it, want))             { if (!viable) viable = m; }
        }
    }
    cxx::MethodDecl *chosen = exact ? exact : viable;
    if (!chosen) return 0;
    checkConstUse(chosen, objectConst, at);
    cxx::ClassDecl *owner = findClass(chosen->ownerClass);
    if (!memberIsAccessible(chosen, owner)) {
        error(at, std::string("'operator[]' is ") + cxx::accessText(memberAccess(chosen))
                  + " in class '" + (owner ? owner->name : cd->name) + "'");
    }
    return chosen;
}

// Every member reached through an object obeys the same const rule, whether it
// is spelled f(), a.f(), a + b or a[i].  Keeping the check in one place is what
// stops each new call form growing a hole of its own.
void SemanticAnalyzer::checkConstUse(cxx::MethodDecl *m, bool objectConst, cc::ASTNode *at) {
    if (!m || !objectConst || m->isConstMethod) return;
    error(at, "'" + m->name + "' is not const, and the object here is");
}

cxx::MethodDecl *SemanticAnalyzer::findMemberOperator(cc::Type *lt, cc::BinaryOp op,
                                                      cc::Expr *rhs, cc::Type *rt,
                                                      cc::ASTNode *at) {
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(stripReference(lt));
    if (!ct) return 0;
    cxx::ClassDecl *cd = findClass(ct->className);
    if (!cd) return 0;

    const std::string name = std::string("operator") + cc::binaryOpText(op);
    const std::vector<cc::Function*> cands = findMethods(cd, name);
    if (cands.empty()) return 0;

    cxx::MethodDecl *exact = 0;
    cxx::MethodDecl *viable = 0;
    for (std::size_t i = 0; i < cands.size(); ++i) {
        cxx::MethodDecl *m = dynamic_cast<cxx::MethodDecl*>(cands[i]);
        if (!m || m->params.size() != 1) continue;
        cc::Type *want = m->params[0]->type;
        if (rt && exactForOverload(rt, want)) { if (!exact) exact = m; continue; }
        if (convertible(rhs, rt, want))               { if (!viable) viable = m; }
    }

    // No member takes this operand -- decline QUIETLY, because a non-member
    // may well take it.  Reporting here made `cout << myObject` impossible:
    // ostream has member <<'s, so the free one was never reached.
    cxx::MethodDecl *chosen = exact ? exact : viable;
    if (!chosen) return 0;

    cxx::ClassDecl *owner = findClass(chosen->ownerClass);
    if (!memberIsAccessible(chosen, owner)) {
        error(at, "'" + chosen->name + "' is " + cxx::accessText(memberAccess(chosen))
                  + " in class '" + (owner ? owner->name : ct->className) + "'");
    }
    checkConstUse(chosen, lt && stripReference(lt)->isConst, at);
    return chosen;
}

cc::Decl *SemanticAnalyzer::findMember(cxx::ClassDecl *cd, const std::string &member,
                                       cxx::ClassDecl **foundIn) {
    for (cxx::ClassDecl *c = cd; c; c = c->base) {
        for (std::size_t i = 0; i < c->members.size(); ++i) {
            cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(c->members[i]);
            if (fd && fd->name == member) { if (foundIn) *foundIn = c; return fd; }
            cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(c->members[i]);
            // Neither takes part in ordinary name lookup.
            if (md && !md->isConstructor && !md->isDestructor && md->name == member) {
                if (foundIn) *foundIn = c;
                return md;
            }
        }
    }
    return 0;
}

bool SemanticAnalyzer::isDerivedFrom(cxx::ClassDecl *derived, cxx::ClassDecl *base) {
    for (cxx::ClassDecl *c = derived; c; c = c->base) {
        if (c == base) return true;
    }
    return false;
}

// A friend is granted access by name, so the question is simply whether the
// function we are inside is one the class named.
bool SemanticAnalyzer::isFriendOf(cxx::ClassDecl *owner) const {
    if (!owner || !currentFunction) return false;
    for (std::size_t i = 0; i < owner->friends.size(); ++i) {
        cc::Function *f = owner->friends[i];
        if (f->name == currentFunction->name && sameParams(f, currentFunction)) return true;
    }
    return false;
}

bool SemanticAnalyzer::memberIsAccessible(cc::Decl *m, cxx::ClassDecl *owner) const {
    const cxx::Access a = memberAccess(m);
    if (a == cxx::ACC_Public) return true;
    // A friend sees everything, from wherever it is written.
    if (isFriendOf(owner)) return true;
    if (currentClass.empty()) return false;
    // const, so findClass() is off limits.
    std::map<std::string, cxx::ClassDecl*>::const_iterator it = classes.find(currentClass);
    cxx::ClassDecl *from = (it == classes.end()) ? 0 : it->second;
    if (!from) return false;
    if (from == owner) return true;                 // inside the class itself
    if (a == cxx::ACC_Protected) return isDerivedFrom(from, owner);
    return false;                                   // private, from outside
}

cxx::ClassDecl *SemanticAnalyzer::ownerClassOf(cc::Decl *m) {
    cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(m);
    if (fd) return findClass(fd->ownerClass);
    cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(m);
    if (md) return findClass(md->ownerClass);
    return 0;
}

bool SemanticAnalyzer::sameSignature(cc::Function *a, cc::Function *b) {
    if (a->params.size() != b->params.size()) return false;
    for (std::size_t i = 0; i < a->params.size(); ++i) {
        if (!sameType(a->params[i]->type, b->params[i]->type)) return false;
    }
    return true;
}

cxx::Access SemanticAnalyzer::memberAccess(cc::Decl *m) {
    cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(m);
    if (fd) return fd->access;
    cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(m);
    if (md) return md->access;
    return cxx::ACC_Public;
}

cc::Type *SemanticAnalyzer::memberType(cc::Decl *m) {
    cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(m);
    if (fd) return fd->type;
    cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(m);
    if (md) return md->retType;
    return 0;
}

// Same name and matching signature over a base virtual is an override.  Same
// name, different signature is legal but almost always a mistake, so it warns.
void SemanticAnalyzer::resolveOverrides(cxx::ClassDecl *cd) {
    if (!cd->base) return;

    // A destructor overrides by POSITION: ~Derived and ~Base are spelled
    // differently but occupy the same vtable slot.
    if (cd->dtor) {
        for (cxx::ClassDecl *b = cd->base; b; b = b->base) {
            if (!b->dtor) continue;
            if (b->dtor->isVirtual) {
                cd->dtor->overrides = b->dtor;
                cd->dtor->isVirtual = true;
            }
            break;
        }
    }

    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(cd->members[i]);
        if (!md || md->isConstructor || md->isDestructor) continue;
        // A base may declare several virtuals of one name.  The one being
        // overridden is the one with the SAME SIGNATURE; taking the first by
        // name gave f(int,int) a vtable slot of its own and sent every call
        // through f(int).
        cxx::MethodDecl *bm = 0;
        cxx::MethodDecl *nameOnly = 0;
        for (cxx::ClassDecl *b = cd->base; b && !bm; b = b->base) {
            for (std::size_t k = 0; k < b->members.size(); ++k) {
                cxx::MethodDecl *cand = dynamic_cast<cxx::MethodDecl*>(b->members[k]);
                if (!cand || cand->isConstructor || cand->isDestructor) continue;
                if (cand->name != md->name) continue;
                if (sameSignature(md, cand)) { bm = cand; break; }
                if (!nameOnly) nameOnly = cand;
            }
        }

        if (!bm) {
            // Same name, no signature matches: hiding, which is legal and
            // almost always a mistake.
            if (nameOnly) {
                diag.warning(md->line, md->col,
                             "'" + md->name + "' hides '" + nameOnly->ownerClass
                             + "::" + nameOnly->name + "' rather than overriding it");
            }
            continue;
        }
        if (!bm->isVirtual) continue;       // hiding a non-virtual is not an override
        if (!sameType(md->retType, bm->retType)) {
            error(md, "'" + md->name + "' overrides '" + bm->ownerClass + "::" + bm->name
                      + "' but returns " + describe(md->retType)
                      + " instead of " + describe(bm->retType));
            continue;
        }
        // Virtualness propagates down the chain whether or not `virtual` is
        // repeated.
        md->overrides = bm;
        md->isVirtual = true;
    }
}

// Most-derived first, so insert() -- which refuses a duplicate -- keeps the
// derived member.  That is name hiding, for free.
void SemanticAnalyzer::pushClassScope(cxx::ClassDecl *cd) {
    symbols.pushScope();
    for (cxx::ClassDecl *c = cd; c; c = c->base) {
        for (std::size_t i = 0; i < c->members.size(); ++i) {
            cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(c->members[i]);
            if (fd) {
                Symbol *s = new Symbol(SYM_Field, fd->name, fd, fd->type);
                if (!symbols.insert(fd->name, s)) delete s;
                continue;
            }
            cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(c->members[i]);
            if (md && !md->isConstructor && !md->isDestructor) {
                Symbol *s = new Symbol(SYM_Method, md->name, md, md->retType);
                if (!symbols.insert(md->name, s)) delete s;
            }
        }
    }
}

// Only a class object whose class or some base declares one.  A pointer never
// does -- `delete` is explicit for a reason.
// Does anything this class owns need destroying?  Its base, or a member that
// is an object (an array of them counts).
bool SemanticAnalyzer::needsDestructor(cxx::ClassDecl *cd) {
    if (!cd) return false;
    if (cd->dtor) return true;
    for (cxx::ClassDecl *b = cd->base; b; b = b->base) if (b->dtor) return true;
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (fd && hasDestructor(fd->type)) return true;
    }
    return false;
}

// Exact ENOUGH to win outright, for overload resolution only.
//
// sameType, plus one rule: void* is exact for any pointer.  Conversions here
// come in two tiers -- exact, then merely possible -- with no ranking inside
// the second, and a pointer can become either a void* or a bool, because
// `if (p)` is the reason bool takes a pointer at all.  So an overload set
// holding both is ambiguous for every pointer, and ostream's holds both.
// Calling void* the exact answer for a pointer settles it the way C++ settles
// it, without a conversion-ranking pass this compiler does not have.
//
// The deviation, and it is a real one: f(Base*) beside f(void*), called with a
// Derived*, picks void* here where C++ picks Base*.
bool SemanticAnalyzer::exactForOverload(cc::Type *arg, cc::Type *param) {
    cc::Type *p = stripReference(param);
    if (sameType(arg, p)) return true;
    cc::PointerType *pa = dynamic_cast<cc::PointerType*>(stripReference(arg));
    cc::PointerType *pp = dynamic_cast<cc::PointerType*>(p);
    return pa != 0 && pp != 0 && isVoid(pp->base);
}

cxx::MethodDecl *SemanticAnalyzer::copyConstructorOf(cxx::ClassDecl *cd) {
    if (!cd) return 0;
    for (std::size_t i = 0; i < cd->ctors.size(); ++i) {
        cxx::MethodDecl *c = cd->ctors[i];
        if (!c || c->params.size() != 1) continue;
        // By reference, necessarily: taking the argument by value would need
        // the very copy being defined.
        cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(c->params[0]->type);
        if (!rt) continue;
        cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(rt->base);
        if (ct && ct->className == cd->name) return c;
    }
    return 0;
}

// Can a copy constructor for this class be GENERATED?  Only if every part its
// initialiser list will name can be constructed from one argument.  A part
// that declares a copy constructor answers yes on the spot; otherwise it must
// be one this same rule can generate, which makes the question recursive.
//
// An array member used to stop the recursion with a no, because no syntax puts
// an array in an initialiser list.  That is true of the syntax and false of the
// consequence: generating nothing left the WHOLE class on the byte copy, so a
// sibling member that DID have a copy constructor never saw one either -- and
// where that sibling owned memory, the two objects ended up holding one
// pointer and the second destructor freed it twice.  An array member is now
// named like any other and lowered as a whole-array move, which is what the
// byte copy always did for it, so the array is copied exactly as before and
// everything beside it is copied properly.
bool SemanticAnalyzer::canSynthesiseCopy(cxx::ClassDecl *cd, int depth) {
    if (!cd) return true;                       // no part is no obstacle
    if (copyConstructorOf(cd)) return true;     // declared: nothing to generate
    if (depth > 64) return false;               // a hierarchy this deep is broken

    if (!canSynthesiseCopy(cd->base, depth + 1)) return false;
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (!fd) continue;
        // Only a member made OF a class is constructed -- an array of them one
        // element at a time.  A pointer or a scalar is copied as the value it is.
        cxx::ClassDecl *fcd = memberClassOf(fd->type);
        if (!fcd) continue;
        if (!canSynthesiseCopy(fcd, depth + 1)) return false;
    }
    return true;
}

bool SemanticAnalyzer::needsCopyConstructor(cxx::ClassDecl *cd) {
    if (!cd || copyConstructorOf(cd)) return false;
    if (!canSynthesiseCopy(cd)) return false;

    for (cxx::ClassDecl *b = cd->base; b; b = b->base) {
        if (copyConstructorOf(b)) return true;
    }
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (!fd) continue;
        if (copyConstructorOf(memberClassOf(fd->type))) return true;
    }
    return false;
}

// The copy constructor the language says exists whether or not it is written.
// Without it a derived object was copied with memcpy, so a base that counted
// its copies never saw one -- the same silent wrong answer a declared copy
// constructor was added to prevent, one level up.
//
// It is built as the initialiser list a reader would have written:
//
//     Derived(const Derived &__o) : Base(__o), extra(__o.extra) { }
//
// so nothing downstream needs to know it was generated.  selectConstructor
// picks the base's and each member's copy constructor exactly as it would for
// a written one, and Lowering::copyConstructorOf finds it in cd->ctors.
//
// Generates the constructor for cd, and FIRST for every base and class-typed
// member its initialiser list will name -- otherwise the list calls a
// constructor that does not exist, and a class whose parts were merely
// default-constructible stopped compiling the moment one of its other parts
// gained a copy constructor.  canSynthesiseCopy has already established that
// the recursion succeeds all the way down.
bool SemanticAnalyzer::synthesiseCopyFor(cxx::ClassDecl *cd) {
    if (!cd || copyConstructorOf(cd) || !canSynthesiseCopy(cd)) return false;

    if (cd->base) synthesiseCopyFor(cd->base);
    for (std::size_t m = 0; m < cd->members.size(); ++m) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[m]);
        if (!fd) continue;
        // An array member's ELEMENT class needs one too: lowering calls it per
        // element, so it must exist by the time lowering looks.
        if (cxx::ClassDecl *fcd = memberClassOf(fd->type)) synthesiseCopyFor(fcd);
    }

    cxx::MethodDecl *c = new cxx::MethodDecl(0, cd->name, cxx::ACC_Public);
    c->ownerClass = cd->name;
    c->isConstructor = true;
    c->isImplicit = true;
    c->line = cd->line;
    c->col = cd->col;

    // const T &__o -- the const rides on the referenced type, which is
    // where every check looks for it.
    cxx::ClassType *arg = new cxx::ClassType(cd->name);
    arg->isConst = true;
    arg->line = cd->line;
    arg->col = cd->col;
    cxx::ReferenceType *ref = new cxx::ReferenceType(arg);
    ref->line = cd->line;
    ref->col = cd->col;
    cc::VarDecl *param = new cc::VarDecl(ref, "__o", 0);
    param->line = cd->line;
    param->col = cd->col;
    c->params.push_back(param);

    // : Base(__o)
    if (cd->base) {
        cxx::MemberInit bi;
        bi.name = cd->base->name;
        bi.line = cd->line;
        bi.col = cd->col;
        cc::IdentExpr *src = new cc::IdentExpr("__o");
        src->line = cd->line;
        src->col = cd->col;
        bi.args.push_back(src);
        c->memberInits.push_back(bi);
    }

    // , each member as  m(__o.m)
    for (std::size_t m = 0; m < cd->members.size(); ++m) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[m]);
        if (!fd) continue;
        cxx::MemberInit mi;
        mi.name = fd->name;
        mi.line = cd->line;
        mi.col = cd->col;
        cc::IdentExpr *base = new cc::IdentExpr("__o");
        base->line = cd->line;
        base->col = cd->col;
        cxx::MemberAccessExpr *acc =
            new cxx::MemberAccessExpr(base, fd->name, false);
        acc->line = cd->line;
        acc->col = cd->col;
        mi.args.push_back(acc);
        c->memberInits.push_back(mi);
    }

    c->body = new cc::CompoundStmt();    // the list is the whole of it
    c->body->line = cd->line;
    c->body->col = cd->col;

    cd->members.push_back(c);
    cd->ctors.push_back(c);
    return true;
}

void SemanticAnalyzer::synthesiseCopyConstructors() {
    // Repeat until nothing changes: giving one class a copy constructor can
    // make a class that holds one need its own.
    bool changed = true;
    while (changed) {
        changed = false;
        std::map<std::string, cxx::ClassDecl*>::iterator it;
        for (it = classes.begin(); it != classes.end(); ++it) {
            if (!it->second || !needsCopyConstructor(it->second)) continue;
            if (synthesiseCopyFor(it->second)) changed = true;
        }
    }
}

void SemanticAnalyzer::synthesiseDestructors() {
    // Repeat until nothing changes: giving one class a destructor can make a
    // class that holds one need its own.
    bool changed = true;
    while (changed) {
        changed = false;
        std::map<std::string, cxx::ClassDecl*>::iterator it;
        for (it = classes.begin(); it != classes.end(); ++it) {
            cxx::ClassDecl *cd = it->second;
            if (!cd || cd->dtor || !needsDestructor(cd)) continue;
            cxx::MethodDecl *d = new cxx::MethodDecl(0, "~" + cd->name, cxx::ACC_Public);
            d->ownerClass = cd->name;
            d->isDestructor = true;
            d->isImplicit = true;
            d->line = cd->line;
            d->col = cd->col;
            d->body = new cc::CompoundStmt();       // nothing of its own to do
            d->body->line = cd->line;
            d->body->col = cd->col;
            cd->members.push_back(d);
            cd->dtor = d;
            changed = true;
        }
    }
}

bool SemanticAnalyzer::hasDestructor(cc::Type *t) {
    if (!t) return false;
    if (dynamic_cast<cc::PointerType*>(t)) return false;
    if (dynamic_cast<cxx::ReferenceType*>(t)) return false;   // a reference owns nothing
    // An array of objects owns every one of them.
    while (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) t = at->element;
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (!ct) return false;
    for (cxx::ClassDecl *c = findClass(ct->className); c; c = c->base) {
        if (c->dtor) return true;
    }
    return false;
}

// Rules about a class as a whole.
void SemanticAnalyzer::checkClassInvariants(cxx::ClassDecl *cd) {
    // A constructor runs BEFORE the object has a vtable pointer to dispatch
    // through -- it is what puts one there -- so there is nothing for
    // `virtual` to mean on it.  Cleared FIRST, so the polymorphism test below
    // is not fooled by a keyword that should never have been there.
    for (std::size_t i = 0; i < cd->ctors.size(); ++i) {
        if (cd->ctors[i]->isVirtual) {
            error(cd->ctors[i], "a constructor cannot be virtual");
            cd->ctors[i]->isVirtual = false;
        }
    }

    // Two members of one name and one signature: the call site could not
    // choose between them.
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::MethodDecl *a = dynamic_cast<cxx::MethodDecl*>(cd->members[i]);
        if (!a || a->isDestructor) continue;
        for (std::size_t j = i + 1; j < cd->members.size(); ++j) {
            cxx::MethodDecl *b = dynamic_cast<cxx::MethodDecl*>(cd->members[j]);
            if (!b || b->name != a->name || b->isDestructor) continue;
            if (!sameParams(a, b)) continue;
            error(b, "'" + cd->name + "::" + a->name + "' is declared twice with the same parameters");
        }
        for (std::size_t j = 0; j < cd->members.size(); ++j) {
            cxx::FieldDecl *f = dynamic_cast<cxx::FieldDecl*>(cd->members[j]);
            if (f && f->name == a->name) {
                error(f, "'" + f->name + "' is both a field and a member function of '"
                         + cd->name + "'");
            }
        }
    }

    if (cd->dtor && !cd->dtor->params.empty()) {
        error(cd->dtor, "a destructor cannot take parameters");
    }

    // A polymorphic class is meant to be used through a base pointer, and
    // deleting through one with a non-virtual destructor runs the wrong one.
    bool polymorphic = false;
    for (cxx::ClassDecl *c = cd; c && !polymorphic; c = c->base) {
        for (std::size_t i = 0; i < c->members.size(); ++i) {
            cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(c->members[i]);
            if (md && md->isVirtual && !md->isDestructor && !md->isConstructor) {
                polymorphic = true;
                break;
            }
        }
    }
    // Only advise about a destructor the user actually wrote; one the compiler
    // generated is not theirs to make virtual.
    if (polymorphic && cd->dtor && !cd->dtor->isVirtual && !cd->dtor->isImplicit) {
        diag.warning(cd->dtor->line, cd->dtor->col,
                     "class '" + cd->name + "' has virtual functions but a non-virtual destructor");
    }
}

// Each name is a field OF THIS CLASS or the base's own name.  An inherited
// field is the base constructor's job, which is why the base gets an entry.
void SemanticAnalyzer::analyzeMemberInits(cxx::MethodDecl *ctor, cxx::ClassDecl *cd) {
    std::vector<std::string> seen;
    int lastFieldIndex = -1;
    bool outOfOrder = false;

    for (std::size_t i = 0; i < ctor->memberInits.size(); ++i) {
        cxx::MemberInit &mi = ctor->memberInits[i];

        for (std::size_t j = 0; j < seen.size(); ++j) {
            if (seen[j] == mi.name) {
                diag.error(mi.line, mi.col, "'" + mi.name + "' is initialised more than once");
            }
        }
        seen.push_back(mi.name);

        // the base class, by name
        if (cd->base && mi.name == cd->base->name) {
            mi.isBase = true;
            if (i != 0) {
                diag.warning(mi.line, mi.col,
                             "the base class initialiser is written after a member, but the base "
                             "is always constructed first");
            }
            for (std::size_t a = 0; a < mi.args.size(); ++a) {
                bool lv = false;
                analyzeExpr(mi.args[a], lv);
            }
            mi.resolvedCtor = selectConstructor(cd->base, mi.args, ctor,
                              "base class '" + cd->base->name + "'");
            continue;
        }

        // otherwise a field declared in THIS class
        int fieldIndex = -1;
        cxx::FieldDecl *field = 0;
        int counter = 0;
        for (std::size_t m = 0; m < cd->members.size(); ++m) {
            cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[m]);
            if (!fd) continue;
            if (fd->name == mi.name) { field = fd; fieldIndex = counter; break; }
            ++counter;
        }
        if (!field) {
            cxx::ClassDecl *owner = 0;
            if (cd->base && findMember(cd->base, mi.name, &owner)) {
                diag.error(mi.line, mi.col,
                           "'" + mi.name + "' is inherited from '" + owner->name
                           + "'; initialise the base instead");
            } else {
                diag.error(mi.line, mi.col,
                           "'" + mi.name + "' is not a member of class '" + cd->name + "'");
            }
            continue;
        }

        if (fieldIndex < lastFieldIndex) outOfOrder = true;
        lastFieldIndex = fieldIndex;

        // An array member, which only a GENERATED constructor names: there is
        // no syntax for it, so a user still reaches the checks below and the
        // error they already gave.  What the entry means is a whole-array
        // move, decided in lowering; here it needs only its source analysed,
        // because lowering asks for that expression's type.
        if (ctor->isImplicit && dynamic_cast<cc::ArrayType*>(field->type)) {
            for (std::size_t a = 0; a < mi.args.size(); ++a) {
                bool lv = false;
                analyzeExpr(mi.args[a], lv);
            }
            continue;
        }

        // A member that is itself an object is CONSTRUCTED here, with whatever
        // arguments are written -- `Outer() : in(7)` calls Inner(int).  Only a
        // scalar member is initialised from a single value.
        if (cxx::ClassType *fct = dynamic_cast<cxx::ClassType*>(field->type)) {
            cxx::ClassDecl *fcd = findClass(fct->className);
            mi.resolvedCtor = selectConstructor(fcd, mi.args, ctor,
                                                "the initialiser for '" + mi.name + "'");
            continue;
        }

        if (mi.args.size() != 1) {
            if (mi.args.empty() && hasDestructor(field->type)) continue;   // default-construct
            if (mi.args.size() > 1) {
                diag.error(mi.line, mi.col,
                           "'" + mi.name + "' takes one initialiser value");
                continue;
            }
        }
        for (std::size_t a = 0; a < mi.args.size(); ++a) {
            bool lv = false;
            cc::Type *at = analyzeExpr(mi.args[a], lv);
            if (at && !convertible(mi.args[a], at, field->type)) {
                diag.error(mi.line, mi.col,
                           "cannot initialise '" + describe(field->type) + " " + mi.name
                           + "' from an expression of type " + describe(at));
            } else if (at) {
                warnIfNarrowing(mi.args[a], at, field->type, mi.args[a],
                                "the initialisation of '" + mi.name + "'");
            }
        }
    }

    // Members are constructed in DECLARATION order, never in the order the
    // list is written.  The bug only shows when one initialiser reads another.
    if (outOfOrder) {
        diag.warning(ctor->line, ctor->col,
                     "initialiser list is out of declaration order; members construct in declaration order");
    }
}

cxx::MethodDecl *SemanticAnalyzer::selectConstructor(cxx::ClassDecl *cd,
                                                     const std::vector<cc::Expr*> &args,
                                                     cc::ASTNode *at, const std::string &what) {
    if (!cd) return 0;
    const std::size_t argCount = args.size();

    // No constructor the USER wrote is legal: the fields are uninitialised, as
    // in C.  A constructor the compiler generated does not change that.  The
    // implicit copy constructor in particular must not take default
    // construction away from a class that never declared anything -- writing
    // nothing cannot be what makes `Derived a;` illegal.
    bool userWritten = false;
    for (std::size_t i = 0; i < cd->ctors.size(); ++i) {
        if (cd->ctors[i] && !cd->ctors[i]->isImplicit) { userWritten = true; break; }
    }

    if (!userWritten && argCount == 0) return 0;

    if (cd->ctors.empty()) {
        if (argCount > 0) {
            error(at, "class '" + cd->name + "' has no constructor to take arguments");
        }
        return 0;
    }

    // By SIGNATURE, like any other overload.  P(int,int) and P(double,double)
    // are two constructors, and choosing on the count alone could only ever
    // reject the pair.
    std::vector<cc::Type*> argTypes;
    for (std::size_t i = 0; i < argCount; ++i) {
        bool lv = false;
        argTypes.push_back(analyzeExpr(args[i], lv));
    }

    std::vector<cxx::MethodDecl*> exact, viable;
    for (std::size_t i = 0; i < cd->ctors.size(); ++i) {
        cxx::MethodDecl *c = cd->ctors[i];
        if (c->params.size() != argCount) continue;
        bool allExact = true, allOk = true;
        for (std::size_t k = 0; k < argCount; ++k) {
            if (!argTypes[k]) continue;
            cc::Type *want = c->params[k]->type;
            if (!exactForOverload(argTypes[k], want)) allExact = false;
            if (!convertible(args[k], argTypes[k], want)) allOk = false;
        }
        if (allExact) exact.push_back(c);
        if (allOk) viable.push_back(c);
    }

    if (exact.size() == 1) return exact[0];
    if (exact.size() > 1) {
        error(at, "ambiguous constructor for class '" + cd->name + "'");
        return exact[0];
    }
    if (viable.size() == 1) return viable[0];
    if (viable.size() > 1) {
        error(at, "ambiguous constructor for class '" + cd->name + "'");
        return viable[0];
    }

    error(at, "class '" + cd->name + "' has no constructor taking "
              + countText(argCount) + " argument(s), needed for " + what);
    return 0;
}

// --- Entry point ---

void SemanticAnalyzer::analyze(const std::vector<cc::Decl*> &units) {
    // 1) every class name, so declarations may refer to a class defined later
    collectClasses(units);
    // 2) link the hierarchy, before anything tries to look a member up
    resolveBases();
    // 2b) give each out-of-line body to the member it belongs to, so the rest
    //     of the pass sees one complete class
    attachOutOfLineDefinitions(units);
    // 2c) a class whose base or whose members need destroying needs a
    //     destructor of its own, whether or not one was written.  Without it
    //     emitEpilogue never runs and those members are never destroyed.
    synthesiseDestructors();
    // 2d) and a class whose base or whose members have a copy constructor
    //     needs one of its own, or copying it is a memcpy and those
    //     constructors never run.  After the destructors, because both walk
    //     the same members and neither depends on the other.
    synthesiseCopyConstructors();
    // 3) work out which methods override which, so virtualness is known before
    //    any body is analysed
    for (std::size_t i = 0; i < units.size(); ++i) {
        cxx::ClassDecl *cd = dynamic_cast<cxx::ClassDecl*>(units[i]);
        if (cd) resolveOverrides(cd);
    }
    // 3b) whole-class rules, once virtualness is settled
    for (std::size_t i = 0; i < units.size(); ++i) {
        cxx::ClassDecl *cd = dynamic_cast<cxx::ClassDecl*>(units[i]);
        if (cd) checkClassInvariants(cd);
    }
    // 4) every top-level name, so functions may call each other in any order
    declareTopLevel(units);
    // 5) bodies
    for (std::size_t i = 0; i < units.size(); ++i) analyzeDecl(units[i]);
}

void SemanticAnalyzer::collectClasses(const std::vector<cc::Decl*> &units) {
    for (std::size_t i = 0; i < units.size(); ++i) {
        cxx::ClassDecl *cd = dynamic_cast<cxx::ClassDecl*>(units[i]);
        if (!cd) continue;
        if (findClass(cd->name)) {
            error(cd, "class '" + cd->name + "' is already defined");
            continue;
        }
        classes[cd->name] = cd;
        cc::Type *ct = new cxx::ClassType(cd->name);
        ownedTypes.push_back(ct);
        Symbol *s = new Symbol(SYM_Type, cd->name, cd, ct);
        if (!symbols.insert(cd->name, s)) {
            error(cd, "'" + cd->name + "' is already declared");
            delete s;
        }
    }
}

bool SemanticAnalyzer::sameParams(cc::Function *a, cc::Function *b) {
    if (a->params.size() != b->params.size()) return false;
    for (std::size_t i = 0; i < a->params.size(); ++i) {
        if (!sameDeclaredType(a->params[i]->type, b->params[i]->type)) return false;
    }
    // A const member function is a different function from its non-const twin;
    // declaring both is the ordinary container idiom, not a redeclaration.
    cxx::MethodDecl *ma = dynamic_cast<cxx::MethodDecl*>(a);
    cxx::MethodDecl *mb = dynamic_cast<cxx::MethodDecl*>(b);
    if (ma && mb && ma->isConstMethod != mb->isConstMethod) return false;
    return true;
}

// An out-of-line definition is a body looking for its declaration.  Moving the
// body onto the member inside the class means everything downstream -- layout,
// vtables, lowering -- sees one complete class and needs no special case.
void SemanticAnalyzer::attachOutOfLineDefinitions(const std::vector<cc::Decl*> &units) {
    for (std::size_t i = 0; i < units.size(); ++i) {
        cxx::MethodDecl *def = dynamic_cast<cxx::MethodDecl*>(units[i]);
        if (!def || def->ownerClass.empty()) continue;

        cxx::ClassDecl *cd = findClass(def->ownerClass);
        if (!cd) {
            error(def, "'" + def->ownerClass + "' is not a class");
            continue;
        }

        cxx::MethodDecl *decl = 0;
        for (std::size_t m = 0; m < cd->members.size(); ++m) {
            cxx::MethodDecl *cand = dynamic_cast<cxx::MethodDecl*>(cd->members[m]);
            if (!cand || cand->name != def->name) continue;
            if (cand->isConstructor != def->isConstructor) continue;
            if (cand->isDestructor != def->isDestructor) continue;
            if (!sameParams(cand, def)) continue;
            decl = cand;
            break;
        }
        if (!decl) {
            error(def, "no member '" + def->name + "' of class '" + def->ownerClass
                       + "' matches this definition");
            continue;
        }
        if (decl->body) {
            error(def, "'" + def->ownerClass + "::" + def->name + "' is already defined");
            continue;
        }
        if (!def->isConstructor && !def->isDestructor &&
            !sameType(decl->retType, def->retType)) {
            error(def, "'" + def->ownerClass + "::" + def->name
                       + "' is defined with a different return type");
            continue;
        }

        // Hand the body and the initialiser list over; the definition node
        // keeps neither, so nothing is deleted twice.
        decl->body = def->body;
        def->body = 0;
        decl->memberInits = def->memberInits;
        def->memberInits.clear();
        // The parameter NAMES come from the definition -- the declaration may
        // have had none.
        for (std::size_t p = 0; p < decl->params.size() && p < def->params.size(); ++p) {
            if (decl->params[p]->name.empty()) decl->params[p]->name = def->params[p]->name;
        }
    }
}

void SemanticAnalyzer::declareTopLevel(const std::vector<cc::Decl*> &units) {
    for (std::size_t i = 0; i < units.size(); ++i) {
        cc::Function *fn = dynamic_cast<cc::Function*>(units[i]);
        // An out-of-line definition is not a free function; its body has
        // already been given to the member it belongs to.
        cxx::MethodDecl *asMethod = dynamic_cast<cxx::MethodDecl*>(units[i]);
        if (asMethod && !asMethod->ownerClass.empty()) continue;
        if (fn) {
            checkNativeDeclaration(fn);
            std::vector<cc::Function*> &set = overloads[fn->name];
            // Two declarations with the same parameters are the same function;
            // a definition following a declaration is normal, two definitions
            // are not.
            bool duplicate = false;
            for (std::size_t k = 0; k < set.size(); ++k) {
                if (!sameParams(set[k], fn)) continue;
                // Two functions cannot differ only in what they return: the
                // call site has no way to say which one it wanted.
                if (!sameType(set[k]->retType, fn->retType)) {
                    error(fn, "'" + fn->name + "' cannot be overloaded on return type alone");
                } else if (set[k]->body && fn->body) {
                    error(fn, "function '" + fn->name + "' is already defined");
                }
                if (fn->body) set[k] = fn;      // the definition wins
                duplicate = true;
                break;
            }
            if (!duplicate) set.push_back(fn);

            // One symbol per name is enough for lookup; the call site consults
            // the overload set.
            if (!symbols.lookup(fn->name)) {
                symbols.insert(fn->name, new Symbol(SYM_Method, fn->name, fn, fn->retType));
            }
            continue;
        }
        cc::VarDecl *vd = dynamic_cast<cc::VarDecl*>(units[i]);
        if (vd) {
            Symbol *s = new Symbol(SYM_Var, vd->name, vd, vd->type);
            if (!symbols.insert(vd->name, s)) {
                error(vd, "variable '" + vd->name + "' is already declared");
                delete s;
            }
            continue;
        }
    }
}

void SemanticAnalyzer::analyzeDecl(cc::Decl *d) {
    if (!d) return;
    cxx::ClassDecl *cd = dynamic_cast<cxx::ClassDecl*>(d);
    if (cd) { analyzeClass(cd); return; }
    // MethodDecl IS a Function, so this one test covers both.
    cc::Function *fn = dynamic_cast<cc::Function*>(d);
    if (fn) { analyzeFunction(fn); return; }
    cc::VarDecl *vd = dynamic_cast<cc::VarDecl*>(d);
    if (vd) { analyzeVarDecl(vd, false); return; }
}

void SemanticAnalyzer::analyzeClass(cxx::ClassDecl *cd) {
    // A field may not shadow a base field: legal C++, but in a teaching subset
    // it is far more often a mistake than an intention.
    if (cd->base) {
        for (std::size_t i = 0; i < cd->members.size(); ++i) {
            cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
            if (!fd) continue;
            cxx::ClassDecl *owner = 0;
            cc::Decl *hidden = findMember(cd->base, fd->name, &owner);
            if (hidden && dynamic_cast<cxx::FieldDecl*>(hidden)) {
                diag.warning(fd->line, fd->col,
                             "field '" + fd->name + "' hides '" + owner->name
                             + "::" + fd->name + "'");
            }
        }
    }
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (fd) {
            checkTypeIsKnown(fd->type, fd, "field '" + fd->name + "'");
            if (isVoid(fd->type)) error(fd, "field '" + fd->name + "' cannot have type void");
            continue;
        }
        cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(cd->members[i]);
        if (md) analyzeFunction(md);
    }
}

// One routine for a free function and a method: a method IS a function, and
// differs only by having a class scope pushed underneath its parameters.
void SemanticAnalyzer::analyzeFunction(cc::Function *fn) {
    cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(fn);

    checkTypeIsKnown(fn->retType, fn, "return type of '" + fn->name + "'");

    cc::Type *savedReturn = currentReturnType;
    const std::string savedClass = currentClass;
    cc::Function *const savedFunction = currentFunction;
    const bool savedCtorDtor = currentIsCtorOrDtor;
    const bool savedMethodConst = currentMethodIsConst;
    currentMethodIsConst = (md && md->isConstMethod);
    currentReturnType = fn->retType;
    currentClass = md ? md->ownerClass : std::string();
    currentFunction = fn;
    currentIsCtorOrDtor = md && (md->isConstructor || md->isDestructor);

    // A by-value object parameter is a copy the callee owns, so the callee
    // destroys it -- on every path out, which is what putting it in the body's
    // exit list buys.  Appended after the locals, so it goes last.
    if (fn->body) {
        for (std::size_t i = 0; i < fn->params.size(); ++i) {
            cc::VarDecl *p = fn->params[i];
            if (p && !p->name.empty() && hasDestructor(p->type)) {
                fn->body->destroyAtExit.push_back(p);
            }
        }
    }

    if (md) pushClassScope(findClass(md->ownerClass));

    symbols.pushScope();                        // parameters
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
        cc::VarDecl *p = fn->params[i];
        checkTypeIsKnown(p->type, p, "parameter '" + p->name + "'");
        if (isVoid(p->type)) error(p, "parameter '" + p->name + "' cannot have type void");
        if (p->name.empty()) continue;
        Symbol *s = new Symbol(SYM_Var, p->name, p, p->type);
        if (!symbols.insert(p->name, s)) {
            error(p, "duplicate parameter name '" + p->name + "'");
            delete s;
        }
    }

    // The initialiser list is checked INSIDE the parameter scope, because that
    // is where its arguments live:  Point(int a) : x(a) { }
    if (md && md->isConstructor) analyzeMemberInits(md, findClass(md->ownerClass));

    if (fn->body) analyzeBlock(fn->body);

    symbols.popScope();                         // parameters
    if (md) symbols.popScope();                 // class members

    currentReturnType = savedReturn;
    currentClass = savedClass;
    currentFunction = savedFunction;
    currentMethodIsConst = savedMethodConst;
    currentIsCtorOrDtor = savedCtorDtor;
}

// --- Statements ---

void SemanticAnalyzer::analyzeBlock(cc::CompoundStmt *block) {
    symbols.pushScope();
    std::vector<cc::VarDecl*> declared;
    for (std::size_t i = 0; i < block->body.size(); ++i) {
        analyzeStmt(block->body[i]);
        // Remember the class-typed locals, in the order they were declared.
        cc::DeclStmt *ds = dynamic_cast<cc::DeclStmt*>(block->body[i]);
        if (ds && ds->var) declared.push_back(ds->var);
    }
    recordScopeExitDestruction(block, declared);
    symbols.popScope();
}

// Exact reverse of construction.  Recording it here lets the lowering phase
// emit the calls on every path out without redoing the scoping.  With no
// exceptions, those paths are just: end of block, return, break.
void SemanticAnalyzer::recordScopeExitDestruction(cc::CompoundStmt *block,
                                                  const std::vector<cc::VarDecl*> &declared) {
    block->destroyAtExit.clear();
    for (std::size_t i = declared.size(); i > 0; --i) {
        cc::VarDecl *vd = declared[i - 1];
        if (hasDestructor(vd->type)) block->destroyAtExit.push_back(vd);
    }
}

void SemanticAnalyzer::analyzeVarDecl(cc::VarDecl *vd, bool declareIt) {
    if (!vd) return;
    // An unknown type makes every later check about this variable noise.
    const bool typeKnown = checkTypeIsKnown(vd->type, vd, "declaration of '" + vd->name + "'");
    if (isVoid(vd->type)) error(vd, "variable '" + vd->name + "' cannot have type void");

    // A const variable can never be assigned to, so its declaration is the
    // only chance it has to be given a value.
    if (vd->type && vd->type->isConst && !vd->init && !vd->hasCtorArgs) {
        // An object with a constructor is initialised BY it, so it needs no
        // initialiser of its own.  Only a scalar is left with nothing.
        cxx::ClassType *cct = dynamic_cast<cxx::ClassType*>(vd->type);
        cxx::ClassDecl *ccd = cct ? findClass(cct->className) : 0;
        if (!ccd || ccd->ctors.empty()) {
            error(vd, "const '" + vd->name + "' must be initialised");
        }
    }

    cc::Type *initType = 0;
    bool initIsLValue = false;
    if (vd->init) initType = analyzeExpr(vd->init, initIsLValue);

    // A reference binds a name to an existing object, so its initialiser must
    // denote one.  `int &s = 1;` has nothing to bind to.  This is what
    // analyzeExpr()'s isLValue result exists for.
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(vd->type);
    if (rt) {
        if (!vd->init) {
            error(vd, "reference '" + vd->name + "' must be initialised");
        } else if (initType && !initIsLValue) {
            error(vd, "cannot bind reference '" + vd->name + "' of type "
                      + describe(vd->type) + " to a non-lvalue initialiser");
        } else if (initType && !convertible(vd->init, initType, vd->type)) {
            // The whole reference type, not rt->base: convertible() has the
            // rule that binding a T& to a const object discards the const,
            // and it can only apply that rule if it can SEE the reference.
            // Passing the base type asked "does const int convert to int?",
            // which is true -- for a copy.  A reference is not a copy, so
            // `const int c; int &r = c;` slipped through and let a const
            // object be assigned to.  The argument path already passes the
            // parameter's own type and has always rejected this.
            error(vd, "cannot bind '" + describe(vd->type) + " " + vd->name
                      + "' to an initialiser of type " + describe(initType));
        }
    } else if (dynamic_cast<cc::ArrayType*>(vd->type)) {
        if (vd->init) error(vd, "an array cannot be initialised from an expression");
    } else if (vd->init && initType && typeKnown && !convertible(vd->init, initType, vd->type)) {
        error(vd, "cannot initialise '" + describe(vd->type) + " " + vd->name
                  + "' from an expression of type " + describe(initType));
    } else if (vd->init && initType) {
        warnIfNarrowing(vd->init, initType, vd->type, vd,
                        "the initialisation of '" + vd->name + "'");
    }

    // A class-typed object is CONSTRUCTED.  `Point p;` needs a constructor
    // taking no arguments whenever the class declares any at all, and
    // `Point q(1, 2)` needs one taking two.
    cxx::ClassDecl *cls = 0;
    {
        cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(stripReference(vd->type));
        if (ct) cls = findClass(ct->className);
    }
    if (cls && !dynamic_cast<cxx::ReferenceType*>(vd->type)) {
        for (std::size_t i = 0; i < vd->ctorArgs.size(); ++i) {
            bool lv = false;
            analyzeExpr(vd->ctorArgs[i], lv);
        }
        if (!vd->init) {
            vd->resolvedCtor = selectConstructor(cls, vd->ctorArgs, vd,
                              "the declaration of '" + vd->name + "'");
        }
    } else if (vd->hasCtorArgs) {
        error(vd, "'" + describe(vd->type) + "' is not a class type, so '" + vd->name
                  + "' cannot be constructed with arguments");
    }

    if (!declareIt) return;
    if (symbols.lookupLocal(vd->name)) {
        error(vd, "redeclaration of '" + vd->name + "' in the same scope");
        return;
    }
    Symbol *s = new Symbol(SYM_Var, vd->name, vd, vd->type);
    if (!symbols.insert(vd->name, s)) delete s;
}

void SemanticAnalyzer::analyzeStmt(cc::Stmt *s) {
    if (!s) return;

    cc::CompoundStmt *block = dynamic_cast<cc::CompoundStmt*>(s);
    if (block) { analyzeBlock(block); return; }

    cc::DeclStmt *ds = dynamic_cast<cc::DeclStmt*>(s);
    if (ds) { analyzeVarDecl(ds->var, true); return; }

    cc::ExprStmt *es = dynamic_cast<cc::ExprStmt*>(s);
    if (es) { bool lv = false; analyzeExpr(es->expr, lv); return; }

    cc::ReturnStmt *rs = dynamic_cast<cc::ReturnStmt*>(s);
    if (rs) {
        bool lv = false;
        cc::Type *got = rs->expr ? analyzeExpr(rs->expr, lv) : 0;
        if (currentIsCtorOrDtor) {
            if (rs->expr) error(rs, "a constructor or destructor cannot return a value");
            return;
        }
        if (!currentReturnType) return;
        if (!rs->expr) {
            if (!isVoid(currentReturnType)) {
                error(rs, "return with no value in a function returning "
                          + describe(currentReturnType));
            }
        } else if (isVoid(currentReturnType)) {
            error(rs, "return with a value in a function returning void");
        } else if (got && !convertible(rs->expr, got, currentReturnType)) {
            error(rs, "returning " + describe(got) + " from a function returning "
                      + describe(currentReturnType));
        } else if (isNonConstReferenceTo(currentReturnType) && isConstExpr(rs->expr)) {
            // Handing out a writable reference to something const gives the
            // caller a way round every check.  A const member function
            // returning T& to one of its own fields is the usual way in.
            error(rs, "cannot return a non-const reference to something const");
        } else if (got) {
            warnIfNarrowing(rs->expr, got, currentReturnType, rs, "this return");
        }
        return;
    }

    cc::IfStmt *is = dynamic_cast<cc::IfStmt*>(s);
    if (is) {
        bool lv = false;
        analyzeExpr(is->cond, lv);
        analyzeStmt(is->thenBranch);
        analyzeStmt(is->elseBranch);
        return;
    }

    cc::DoWhileStmt *dws = dynamic_cast<cc::DoWhileStmt*>(s);
    if (dws) {
        ++loopDepth;
        analyzeStmt(dws->body);
        --loopDepth;
        bool lv = false;
        analyzeExpr(dws->cond, lv);
        return;
    }

    cc::SwitchStmt *sw = dynamic_cast<cc::SwitchStmt*>(s);
    if (sw) { analyzeSwitch(sw); return; }

    if (dynamic_cast<cc::CaseStmt*>(s)) {
        if (switchDepth == 0) error(s, "a case label outside a switch");
        return;
    }

    cc::WhileStmt *ws = dynamic_cast<cc::WhileStmt*>(s);
    if (ws) {
        bool lv = false;
        analyzeExpr(ws->cond, lv);
        ++loopDepth;
        analyzeStmt(ws->body);
        --loopDepth;
        return;
    }

    cc::ForStmt *fs = dynamic_cast<cc::ForStmt*>(s);
    if (fs) {
        symbols.pushScope();            // the init declaration is scoped to the loop
        analyzeStmt(fs->init);
        bool lv = false;
        if (fs->cond) analyzeExpr(fs->cond, lv);
        if (fs->step) analyzeExpr(fs->step, lv);
        ++loopDepth;
        analyzeStmt(fs->body);
        --loopDepth;
        symbols.popScope();
        return;
    }

    if (dynamic_cast<cc::BreakStmt*>(s)) {
        if (loopDepth == 0 && switchDepth == 0) error(s, "'break' outside a loop or switch");
        return;
    }
    if (dynamic_cast<cc::ContinueStmt*>(s)) {
        if (loopDepth == 0) error(s, "'continue' outside a loop");
        return;
    }
}

// A switch needs an integer subject, and its labels must be distinct -- two
// cases with the same value would make one unreachable.
void SemanticAnalyzer::analyzeSwitch(cc::SwitchStmt *s) {
    bool lv = false;
    cc::Type *ct = analyzeExpr(s->cond, lv);
    cc::BuiltinKind k;
    if (ct && (!arithmeticKind(ct, k) || !cc::builtinIsInteger(k))) {
        error(s, "a switch needs an integer subject, not " + describe(ct));
    }

    std::vector<long> seen;
    bool sawDefault = false;
    if (s->body) {
        for (std::size_t i = 0; i < s->body->body.size(); ++i) {
            cc::CaseStmt *c = dynamic_cast<cc::CaseStmt*>(s->body->body[i]);
            if (!c) continue;
            if (c->isDefault) {
                if (sawDefault) error(c, "a switch may have only one 'default'");
                sawDefault = true;
                continue;
            }
            for (std::size_t j = 0; j < seen.size(); ++j) {
                if (seen[j] == c->value) {
                    error(c, "duplicate case label");
                    break;
                }
            }
            seen.push_back(c->value);
        }
    }

    ++switchDepth;
    analyzeStmt(s->body);
    --switchDepth;
}

// Exact match first, then a single viable one.  Real C++ ranks conversions in
// far more detail; arity plus exactness settles everything this subset can say.
std::vector<cc::Function*> SemanticAnalyzer::findMethods(cxx::ClassDecl *cd,
                                                         const std::string &name) {
    std::vector<cc::Function*> out;
    for (cxx::ClassDecl *c = cd; c; c = c->base) {
        for (std::size_t i = 0; i < c->members.size(); ++i) {
            cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(c->members[i]);
            if (!md || md->isConstructor || md->isDestructor) continue;
            if (md->name != name) continue;
            // A derived overload with the same parameters hides the base one.
            bool hidden = false;
            for (std::size_t k = 0; k < out.size(); ++k) {
                if (sameParams(out[k], md)) { hidden = true; break; }
            }
            if (!hidden) out.push_back(md);
        }
    }
    return out;
}

cc::Function *SemanticAnalyzer::resolveOverload(const std::vector<cc::Function*> &candidates,
                                                cc::CallExpr *call, const std::string &name) {
    if (candidates.empty()) return 0;
    if (candidates.size() == 1) return candidates[0];

    std::vector<cc::Type*> argTypes;
    for (std::size_t i = 0; i < call->args.size(); ++i) {
        bool lv = false;
        argTypes.push_back(analyzeExpr(call->args[i], lv));
    }

    std::vector<cc::Function*> exact, viable;
    for (std::size_t c = 0; c < candidates.size(); ++c) {
        cc::Function *f = candidates[c];
        if (f->params.size() != argTypes.size()) continue;
        bool allExact = true, allOk = true;
        for (std::size_t i = 0; i < argTypes.size(); ++i) {
            if (!argTypes[i]) continue;
            if (!exactForOverload(argTypes[i], f->params[i]->type)) allExact = false;
            if (!convertible(call->args[i], argTypes[i], f->params[i]->type)) allOk = false;
        }
        if (allExact) exact.push_back(f);
        if (allOk) viable.push_back(f);
    }

    if (exact.size() == 1) return exact[0];
    if (exact.size() > 1) {
        error(call, "ambiguous call to '" + name + "'");
        return exact[0];
    }
    if (viable.size() == 1) return viable[0];
    if (viable.size() > 1) {
        error(call, "ambiguous call to '" + name + "'");
        return viable[0];
    }
    error(call, "no overload of '" + name + "' takes these arguments");
    return 0;
}

// A function with no body whose NAME is a native's is bound to that native --
// that declaration is the whole of the binding, there being no linker in this
// pipeline to check it against a symbol.  Nothing checked it against anything,
// so a declaration that merely spelled the name right was accepted and the
// call went through with whatever the machine's own signature was:
//
//     int sqrt(int x);      called as sqrt(4)     -> 0
//     double pow(double a); called as pow(2.0)    -> 1
//
// Both compiled, ran, and printed a wrong number in silence.  The table knows
// how many arguments each native takes and whether it answers in floating
// point, so a declaration that disagrees is refused by name here.  What the
// table does not record is each parameter's type -- except that a native
// answering in floating point takes floating point throughout, which is true
// of every entry that does.
namespace {
// Floating in the type model's own terms; a non-builtin is not.
bool declaresFloating(cc::Type *t) {
    cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(t);
    return bt != 0 && cc::builtinIsFloating(bt->kind);
}
}

void SemanticAnalyzer::checkNativeDeclaration(cc::Function *fn) {
    if (!fn || fn->body) return;                       // a body is its own binding
    const NativeId id = nativeByName(fn->name);
    if (id == NAT_Count) return;                       // not a native at all

    const std::size_t want = static_cast<std::size_t>(nativeArgCount(id));
    if (fn->params.size() != want) {
        error(fn, "'" + fn->name + "' is built in and takes " + countText(want)
                  + " argument(s), not " + countText(fn->params.size()));
        return;
    }

    const bool wantsFloat = nativeReturnsFloat(id);
    const bool declaresFloat = declaresFloating(fn->retType);
    if (wantsFloat != declaresFloat) {
        error(fn, "'" + fn->name + "' is built in and returns "
                  + (wantsFloat ? "a floating-point value" : "an integer")
                  + ", not " + describe(fn->retType));
        return;
    }
    if (wantsFloat) {
        for (std::size_t i = 0; i < fn->params.size(); ++i) {
            if (!declaresFloating(fn->params[i]->type)) {
                error(fn, "'" + fn->name + "' is built in and takes floating-point "
                          "arguments; " + parameterText(fn, i) + " is "
                          + describe(fn->params[i]->type));
                return;
            }
        }
    }
}

// --- Calls ---

void SemanticAnalyzer::checkCallArgs(cc::CallExpr *call, cc::Function *fn) {
    if (call->args.size() != fn->params.size()) {
        error(call, "'" + fn->name + "' expects " + countText(fn->params.size())
                    + " argument(s) but got " + countText(call->args.size()));
        return;
    }
    for (std::size_t i = 0; i < call->args.size(); ++i) {
        bool lv = false;
        cc::Type *at = analyzeExpr(call->args[i], lv);
        cc::Type *pt = fn->params[i]->type;
        if (!at || !pt) continue;
        // a reference parameter needs an lvalue, same rule as a variable
        cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(pt);
        if (rt && !lv) {
            error(call->args[i], "argument to reference " + parameterText(fn, i)
                                 + " must be an lvalue");
            continue;
        }
        if (!convertible(call->args[i], at, pt)) {
            error(call->args[i], "argument " + describe(at) + " does not match "
                                 + parameterText(fn, i) + " of type " + describe(pt));
        } else {
            warnIfNarrowing(call->args[i], at, pt, call->args[i],
                            "the argument to " + parameterText(fn, i));
        }
    }
}

// --- Expressions: one walk over a tree that mixes cc:: and cxx:: nodes ---

// Records what the analysis concluded, so lowering never has to guess.
cc::Type *SemanticAnalyzer::analyzeExpr(cc::Expr *e, bool &isLValue) {
    cc::Type *t = analyzeExprImpl(e, isLValue);
    if (e) e->resolvedType = t;
    return t;
}

cc::Type *SemanticAnalyzer::analyzeExprImpl(cc::Expr *e, bool &isLValue) {
    isLValue = false;
    if (!e) return 0;

    // --- LAYER 2 forms, tested first: their operands are expressions ---

    cxx::MemberAccessExpr *ma = dynamic_cast<cxx::MemberAccessExpr*>(e);
    if (ma) {
        bool baseLV = false;
        cc::Type *baseType = analyzeExpr(ma->base, baseLV);
        if (!baseType) return 0;
        if (ma->isArrow) {
            cc::PointerType *pt = dynamic_cast<cc::PointerType*>(baseType);
            if (!pt) { error(ma, "'->' applied to " + describe(baseType) + ", which is not a pointer"); return 0; }
            baseType = pt->base;
        } else if (dynamic_cast<cc::PointerType*>(baseType)) {
            error(ma, "'.' applied to a pointer; did you mean '->'?");
            return 0;
        }
        cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(baseType);
        if (!ct) { error(ma, "member access on non-class type " + describe(baseType)); return 0; }
        cxx::ClassDecl *cd = findClass(ct->className);
        if (!cd) { error(ma, "unknown class '" + ct->className + "'"); return 0; }
        cxx::ClassDecl *owner = 0;
        cc::Decl *m = findMember(cd, ma->member, &owner);
        if (!m) { error(ma, "no member named '" + ma->member + "' in class '" + ct->className + "'"); return 0; }
        if (!memberIsAccessible(m, owner)) {
            error(ma, "'" + ma->member + "' is " + cxx::accessText(memberAccess(m))
                      + " in class '" + owner->name + "'");
        }
        isLValue = (dynamic_cast<cxx::FieldDecl*>(m) != 0);
        if (dynamic_cast<cc::ArrayType*>(stripReference(memberType(m)))) {
            isLValue = false;
            return decay(memberType(m));
        }
        return stripReference(memberType(m));
    }

    cxx::ThisExpr *te = dynamic_cast<cxx::ThisExpr*>(e);
    if (te) {
        if (currentClass.empty()) { error(te, "'this' used outside a member function"); return 0; }
        cxx::ClassType self(currentClass);
        // Inside a const member function `this` points at a const object, so
        // everything reached through it is const.
        self.isConst = currentMethodIsConst;
        return makePointerTo(&self);
    }

    // T(args) -- an object built in place.  Its type is T; it is not an lvalue,
    // because it has no name to be one under.
    cxx::TempExpr *tmp = dynamic_cast<cxx::TempExpr*>(e);
    if (tmp) {
        checkTypeIsKnown(tmp->type, tmp, "a temporary");
        cxx::ClassType *tct = dynamic_cast<cxx::ClassType*>(tmp->type);
        cxx::ClassDecl *tcd = tct ? findClass(tct->className) : 0;
        tmp->resolvedCtor = selectConstructor(tcd, tmp->args, tmp,
                                              "a temporary " + describe(tmp->type));
        isLValue = false;
        return tmp->type;
    }

    cxx::NewExpr *ne = dynamic_cast<cxx::NewExpr*>(e);
    if (ne) {
        checkTypeIsKnown(ne->allocType, ne, "'new' expression");
        if (ne->count) {
            bool lv = false;
            cc::Type *nt = analyzeExpr(ne->count, lv);
            cc::BuiltinKind nk;
            if (nt && (!arithmeticKind(nt, nk) || !cc::builtinIsInteger(nk))) {
                error(ne, "the element count of 'new[]' must be an integer, not "
                          + describe(nt));
            }
        }
        for (std::size_t i = 0; i < ne->args.size(); ++i) {
            bool lv = false;
            analyzeExpr(ne->args[i], lv);
        }
        // new allocates AND constructs, so the arguments must match a ctor.
        cxx::ClassType *nct = dynamic_cast<cxx::ClassType*>(ne->allocType);
        if (ne->count) {
            // Every element gets the same constructor and there is nowhere to
            // write arguments for each, so the array form takes none and the
            // elements are default-constructed -- as in C++.
            if (!ne->args.empty()) {
                error(ne, "'new " + describe(ne->allocType)
                          + "[]' cannot take constructor arguments; its elements are"
                            " default-constructed");
            }
            if (nct) {
                std::vector<cc::Expr*> none;
                ne->resolvedCtor = selectConstructor(findClass(nct->className), none, ne,
                                                     "'new " + nct->className + "[]'");
            }
        } else if (nct) {
            ne->resolvedCtor = selectConstructor(findClass(nct->className),
                                                 ne->args, ne, "'new'");
        } else if (!ne->args.empty()) {
            error(ne, "'new " + describe(ne->allocType) + "' cannot take constructor arguments");
        }
        // new T yields T*, and the T belongs to the AST node, so it is copied
        return makePointerTo(ne->allocType);
    }

    cxx::DeleteExpr *de = dynamic_cast<cxx::DeleteExpr*>(e);
    if (de) {
        bool lv = false;
        cc::Type *t = analyzeExpr(de->operand, lv);
        cc::PointerType *pt = t ? dynamic_cast<cc::PointerType*>(t) : 0;
        if (t && !pt) {
            error(de, std::string(de->isArray ? "'delete[]'" : "'delete'") + " applied to "
                      + describe(t) + ", which is not a pointer");
        } else if (pt) {
            // Only a virtual destructor is found through the vtable.
            cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(pt->base);
            cxx::ClassDecl *cd = ct ? findClass(ct->className) : 0;
            if (cd && cd->dtor && !cd->dtor->isVirtual) {
                bool anyDerived = false;
                std::map<std::string, cxx::ClassDecl*>::iterator it;
                for (it = classes.begin(); it != classes.end(); ++it) {
                    if (it->second != cd && isDerivedFrom(it->second, cd)) { anyDerived = true; break; }
                }
                if (anyDerived) {
                    diag.warning(de->line, de->col,
                                 "deleting through '" + describe(t) + "' with a non-virtual destructor");
                }
            }
        }
        return makeBuiltin(cc::BK_Void);
    }

    // --- LAYER 1 forms ---

    cc::IdentExpr *id = dynamic_cast<cc::IdentExpr*>(e);
    if (id) {
        Symbol *s = symbols.lookup(id->name);
        if (!s) { error(id, "undeclared identifier '" + id->name + "'"); return 0; }
        if (s->kind == SYM_Type) { error(id, "type '" + id->name + "' used as an expression"); return 0; }
        // May have come from a base class scope, so it needs the same access
        // check obj.member gets -- or omitting `this->` would defeat it.
        if (s->kind == SYM_Field || s->kind == SYM_Method) {
            cc::Decl *m = dynamic_cast<cc::Decl*>(s->decl);
            cxx::ClassDecl *owner = m ? ownerClassOf(m) : 0;
            if (owner && !memberIsAccessible(m, owner)) {
                error(id, "'" + id->name + "' is " + cxx::accessText(memberAccess(m))
                          + " in class '" + owner->name + "'");
                return 0;
            }
        }
        // An array does not decay to an lvalue: `a` cannot be assigned to.
        if (dynamic_cast<cc::ArrayType*>(stripReference(s->type))) {
            isLValue = false;
            return decay(s->type);
        }
        isLValue = (s->kind == SYM_Var || s->kind == SYM_Field);
        return stripReference(s->type);
    }

    if (dynamic_cast<cxx::BoolExpr*>(e)) return makeBool();
    if (cc::NumberExpr *n = dynamic_cast<cc::NumberExpr*>(e)) return makeBuiltin(n->kind);
    if (cc::FloatExpr *fl = dynamic_cast<cc::FloatExpr*>(e)) return makeBuiltin(fl->kind);
    if (dynamic_cast<cc::StringExpr*>(e)) {
        // A string literal is a pointer into read-only data.
        cc::Type *p = new cc::PointerType(new cc::BuiltinType(cc::BK_Char));
        ownedTypes.push_back(p);
        return p;
    }

    cc::CallExpr *call = dynamic_cast<cc::CallExpr*>(e);
    if (call) {
        // Resolve the callee to a function without treating it as a value.
        cc::Function *fn = 0;
        cc::IdentExpr *cid = dynamic_cast<cc::IdentExpr*>(call->callee);
        // A bare name inside a member function that names a method of this
        // class IS `this->name`, and must resolve through the same path: the
        // member path is where virtual dispatch, overload resolution by
        // argument type, and the const check all live.  Resolving it here
        // instead gave three different wrong answers.
        if (cid && !currentClass.empty() && !symbols.lookupLocal(cid->name)) {
            std::map<std::string, cxx::ClassDecl*>::iterator ci = classes.find(currentClass);
            cxx::ClassDecl *self = (ci == classes.end()) ? 0 : ci->second;
            if (self && !findMethods(self, cid->name).empty()) {
                cxx::MemberAccessExpr *rewritten =
                    new cxx::MemberAccessExpr(new cxx::ThisExpr(), cid->name, true);
                rewritten->line = cid->line;
                rewritten->col = cid->col;
                delete call->callee;
                call->callee = rewritten;
                cid = 0;
            }
        }

        if (cid) {
            std::map<std::string, std::vector<cc::Function*> >::iterator ov =
                overloads.find(cid->name);
            if (ov != overloads.end()) {
                fn = resolveOverload(ov->second, call, cid->name);
                if (!fn) return 0;
            } else {
                Symbol *s = symbols.lookup(cid->name);
                if (!s) { error(cid, "undeclared function '" + cid->name + "'"); return 0; }
                fn = dynamic_cast<cc::Function*>(s->decl);
                // obj(args) -- not a function at all, but an object whose
                // class overloads the call operator.
                if (!fn && s->type && isClassType(s->type)) {
                    if (cxx::MethodDecl *op = findCallOperator(s->type, call)) {
                        call->resolved = op;
                        checkCallArgs(call, op);
                        if (dynamic_cast<cxx::ReferenceType*>(op->retType)) isLValue = true;
                        return stripReference(op->retType);
                    }
                    error(call, "class '" + describe(s->type) + "' has no operator()");
                    return 0;
                }
                if (!fn) { error(call, "'" + cid->name + "' is not a function"); return 0; }
            }
        } else {
            cxx::MemberAccessExpr *cma = dynamic_cast<cxx::MemberAccessExpr*>(call->callee);
            if (!cma) { error(call, "expression is not callable"); return 0; }
            bool baseLV = false;
            cc::Type *baseType = analyzeExpr(cma->base, baseLV);
            if (!baseType) return 0;
            if (cma->isArrow) {
                cc::PointerType *pt = dynamic_cast<cc::PointerType*>(baseType);
                if (!pt) { error(cma, "'->' applied to a non-pointer"); return 0; }
                baseType = pt->base;
            }
            cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(baseType);
            if (!ct) { error(cma, "method call on non-class type " + describe(baseType)); return 0; }
            cxx::ClassDecl *cd = findClass(ct->className);
            cxx::ClassDecl *owner = 0;
            cc::Decl *m = cd ? findMember(cd, cma->member, &owner) : 0;
            if (!m) { error(cma, "no member named '" + cma->member + "' in class '" + ct->className + "'"); return 0; }
            if (!memberIsAccessible(m, owner)) {
                error(cma, "'" + cma->member + "' is " + cxx::accessText(memberAccess(m))
                           + " in class '" + owner->name + "'");
            }
            const std::vector<cc::Function*> cands = findMethods(cd, cma->member);
            if (cands.empty()) {
                error(cma, "'" + cma->member + "' is not a method");
                return 0;
            }
            fn = resolveOverload(cands, call, cma->member);
            if (!fn) return 0;

            // A const object may only be asked for a const method: anything
            // else is free to modify it.
            cxx::MethodDecl *chosen = dynamic_cast<cxx::MethodDecl*>(fn);
            if (chosen && !chosen->isConstMethod && objectIsConst(cma)) {
                error(cma, "'" + cma->member + "' is not const, and '"
                           + ct->className + "' here is");
            }
        }
        // Recorded so lowering does not resolve the call a second time.
        call->resolved = fn;
        checkCallArgs(call, fn);
        // A function returning T& names an object, so its result may be
        // assigned to.
        if (dynamic_cast<cxx::ReferenceType*>(fn->retType)) isLValue = true;
        return stripReference(fn->retType);
    }

    cc::UnaryExpr *ue = dynamic_cast<cc::UnaryExpr*>(e);
    if (ue) {
        bool operandLV = false;
        cc::Type *t = analyzeExpr(ue->operand, operandLV);
        if (!t) return 0;

        // ++ and -- read and write their operand, so it must be an object --
        // the same rule assignment obeys.
        if (cc::unaryOpIsIncDec(ue->op)) {
            if (!operandLV) {
                error(ue, std::string("'") + cc::unaryOpText(ue->op)
                          + "' needs an lvalue");
                return 0;
            }
            if (isConstExpr(ue->operand)) {
                error(ue, std::string("cannot apply '") + cc::unaryOpText(ue->op)
                          + "' to " + describe(t));
                return 0;
            }
            cc::BuiltinKind k;
            const bool arith = arithmeticKind(t, k);
            if (!arith && !dynamic_cast<cc::PointerType*>(stripReference(t))) {
                error(ue, std::string("'") + cc::unaryOpText(ue->op)
                          + "' needs an arithmetic or pointer operand, got " + describe(t));
                return 0;
            }
            return t;
        }

        switch (ue->op) {
        case cc::UN_Neg: {
            // An object has no sign of its own.  A class that wants one says
            // so, and then this expression is a call to what it said.
            if (isClassType(t)) {
                cc::Function *op = findUnaryMinusOperator(ue->operand, t, ue);
                if (!op) {
                    error(ue, "no 'operator-' for " + describe(t));
                    return 0;
                }
                ue->resolvedOperator = op;
                return op->retType;
            }
            cc::BuiltinKind k;
            if (!arithmeticKind(t, k)) {
                error(ue, "unary '-' needs an arithmetic type, got " + describe(t));
                return 0;
            }
            return makeBuiltin(promote(k));
        }
        case cc::UN_Not:
            // Anything with a zero can be tested; an object has none.
            if (dynamic_cast<cxx::ClassType*>(stripReference(t))) {
                error(ue, "'!' needs a scalar, got " + describe(t));
                return 0;
            }
            return makeBool();
        case cc::UN_Deref: {
            cc::PointerType *pt = dynamic_cast<cc::PointerType*>(decay(t));
            if (!pt) { error(ue, "unary '*' applied to " + describe(t) + ", which is not a pointer"); return 0; }
            // *p on a pointer-to-array yields an array, which decays in turn.
            // That is what makes g[1][2] reach the right element.
            if (dynamic_cast<cc::ArrayType*>(pt->base)) {
                isLValue = false;
                return decay(pt->base);
            }
            isLValue = true;
            return pt->base;
        }
        case cc::UN_AddrOf: {
            if (!operandLV) { error(ue, "cannot take the address of a non-lvalue"); return 0; }
            return makePointerTo(t);
        }
        default:
            break;
        }
        return 0;
    }

    cc::CastExpr *ce = dynamic_cast<cc::CastExpr*>(e);
    if (ce) {
        bool lv = false;
        cc::Type *from = analyzeExpr(ce->expr, lv);
        checkTypeIsKnown(ce->type, ce, "a cast");
        // A cast is the programmer overriding the conversion rules, so it is
        // not checked against them -- but it still must not invent a value out
        // of nothing.
        if (from && isVoid(ce->type)) return makeBuiltin(cc::BK_Void);
        return ce->type;
    }

    // a[i] -- a class overloads it, everything else is pointer arithmetic.
    cc::IndexExpr *ie = dynamic_cast<cc::IndexExpr*>(e);
    if (ie) {
        bool baseLV = false, idxLV = false;
        cc::Type *bt = analyzeExpr(ie->base, baseLV);
        cc::Type *it = analyzeExpr(ie->index, idxLV);
        if (!bt) return 0;

        if (isClassType(bt)) {
            cxx::ClassDecl *cd = findClass(
                dynamic_cast<cxx::ClassType*>(stripReference(bt))->className);
            if (cxx::MethodDecl *op = findIndexOperator(cd, ie->index, it,
                                                       isConstExpr(ie->base), ie)) {
                ie->resolvedOperator = op;
                // T& out means the subscript names an object, so it may be
                // assigned to -- which is the whole point of  s[i] = 'x'.
                cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(op->retType);
                isLValue = (rt != 0);
                return rt ? rt->base : op->retType;
            }
            error(ie, "class '" + cd->name + "' has no operator[]");
            return 0;
        }

        cc::BuiltinKind ik;
        if (it && (!arithmeticKind(it, ik) || !cc::builtinIsInteger(ik))) {
            error(ie, "a subscript must be an integer, not " + describe(it));
        }
        cc::PointerType *pt = dynamic_cast<cc::PointerType*>(decay(bt));
        if (!pt) { error(ie, describe(bt) + " cannot be subscripted"); return 0; }
        // An array of arrays yields an array, which decays in turn -- that is
        // what makes g[1][2] reach the right element.
        if (dynamic_cast<cc::ArrayType*>(pt->base)) {
            isLValue = false;
            return decay(pt->base);
        }
        isLValue = true;
        return pt->base;
    }

    cc::BinaryExpr *be = dynamic_cast<cc::BinaryExpr*>(e);
    if (be) {
        bool lL = false, lR = false;
        cc::Type *lt = analyzeExpr(be->lhs, lL);
        cc::Type *rt = analyzeExpr(be->rhs, lR);
        if (!lt || !rt) return 0;       // the operand already reported its error

        // An operand that is an object sends the whole expression looking for
        // a member named for the operator.  Found, this is a call, and every
        // rule below belongs to the builtin operators it is not.
        if (cc::Function *op = findOperator(be->lhs, lt, be->rhs, rt, be->op, be)) {
            be->resolvedOperator = op;
            isLValue = false;
            return op->retType;
        }

        if (cc::binaryOpIsAssignment(be->op)) {
            // The other half of lvalue-ness: only an object can be assigned to.
            if (!lL) { error(be, "left side of assignment is not an lvalue"); return 0; }
            if (isConstExpr(be->lhs)) {
                // The const may be on the type itself, or inherited from the
                // object it belongs to -- and saying which is the difference
                // between a useful message and a puzzling one.
                cc::Type *slt = stripReference(lt);
                if (slt && slt->isConst) error(be, "cannot assign to " + describe(lt));
                else                     error(be, "cannot modify a const object");
                return 0;
            }

            // A compound assignment on an object needs an operator of its own.
            // Without one there is nothing sensible to do -- and doing the
            // arithmetic on the object's bytes, which is what fell out before,
            // is not an approximation of one.
            if (be->op != cc::BIN_Assign && (isClassType(lt) || isClassType(rt))) {
                error(be, "no 'operator" + std::string(cc::binaryOpText(be->op))
                          + "' for " + describe(lt));
                return 0;
            }
            if (!convertible(be->rhs, rt, lt)) {
                error(be, "cannot assign " + describe(rt) + " to " + describe(lt));
                return 0;
            }
            warnIfNarrowing(be->rhs, rt, lt, be, "this assignment");
            isLValue = true;            // in C++ an assignment yields an lvalue
            return lt;
        }


        if (cc::binaryOpIsLogical(be->op)) {
            // && and || only ask whether each side is true, and anything that
            // converts to bool can answer that -- a number, or a pointer.
            if (!isTestable(lt) || !isTestable(rt)) {
                error(be, std::string("'") + cc::binaryOpText(be->op)
                          + "' needs testable operands, not "
                          + describe(lt) + " and " + describe(rt));
                return 0;
            }
            return makeBool();
        }

        // Shift is not the usual arithmetic conversion: the result has the
        // LEFT operand's type, and the right one only says how far.
        if (be->op == cc::BIN_Shl || be->op == cc::BIN_Shr) {
            cc::BuiltinKind kl, kr;
            if (!arithmeticKind(lt, kl) || !cc::builtinIsInteger(kl) ||
                !arithmeticKind(rt, kr) || !cc::builtinIsInteger(kr)) {
                error(be, std::string("'") + cc::binaryOpText(be->op)
                          + "' needs integer operands, not "
                          + describe(lt) + " and " + describe(rt));
                return 0;
            }
            return makeBuiltin(promote(kl));
        }

        if (cc::binaryOpIsComparison(be->op)) {
            cc::BuiltinKind kl, kr;
            const bool bothArith = arithmeticKind(lt, kl) && arithmeticKind(rt, kr);
            // Two pointers compare when one converts to the other, and a
            // pointer compares with the literal 0 -- which is how a linked
            // list finds its end.
            const bool ptrCompare =
                (dynamic_cast<cc::PointerType*>(stripReference(decay(lt))) ||
                 dynamic_cast<cc::PointerType*>(stripReference(decay(rt)))) &&
                (convertible(be->rhs, rt, lt) || convertible(be->lhs, lt, rt));
            if (!bothArith && !ptrCompare) {
                error(be, std::string("cannot compare ") + describe(lt) + " with " + describe(rt));
                return 0;
            }
            // In C++ a comparison yields bool.
            return makeBool();
        }

        // Pointer arithmetic: p + n and p - n step by whole objects, so the
        // result is still a pointer.  p - q counts the objects between them.
        // Arrays decay first, which is what lets a + 1 work at all.
        lt = decay(lt);
        rt = decay(rt);
        cc::PointerType *pl = dynamic_cast<cc::PointerType*>(stripReference(lt));
        cc::PointerType *pr = dynamic_cast<cc::PointerType*>(stripReference(rt));
        if (pl || pr) {
            cc::BuiltinKind ik;
            const bool lIsInt = builtinKindOf(lt, ik) && cc::builtinIsInteger(ik);
            const bool rIsInt = builtinKindOf(rt, ik) && cc::builtinIsInteger(ik);
            if (be->op == cc::BIN_Add && pl && rIsInt) return lt;
            if (be->op == cc::BIN_Add && pr && lIsInt) return rt;
            if (be->op == cc::BIN_Sub && pl && rIsInt) return lt;
            if (be->op == cc::BIN_Sub && pl && pr && sameType(lt, rt)) {
                return makeBuiltin(cc::BK_Long);
            }
            error(be, std::string("invalid pointer arithmetic: ") + describe(lt)
                      + " " + cc::binaryOpText(be->op) + " " + describe(rt));
            return 0;
        }

        // Arithmetic: both operands meet in a common type, and that is the
        // type of the result.  % is integers only.
        cc::BuiltinKind kl, kr;
        if (arithmeticKind(lt, kl) && arithmeticKind(rt, kr)) {
            if (be->op == cc::BIN_Mod &&
                (cc::builtinIsFloating(kl) || cc::builtinIsFloating(kr))) {
                error(be, "'%' needs integer operands, not "
                          + describe(lt) + " and " + describe(rt));
                return 0;
            }
            return makeBuiltin(usualArithmetic(kl, kr));
        }
        error(be, std::string("invalid operands to '") + cc::binaryOpText(be->op)
                  + "': " + describe(lt) + " and " + describe(rt));
        return 0;
    }

    error(e, "unhandled expression kind in the semantic analyzer");
    return 0;
}
