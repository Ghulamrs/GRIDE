// Lower.cpp
//
// C++98 only.  See Lower.h for the address/value idea this pass turns on.

#include "Lower.h"

#include <cstddef>

namespace cc {

Lowering::Lowering(IRModule &module, const Layout &l, Diagnostics &d)
    : mod(module), layout(l), diag(d), fn(0), objectDest(IR_NoReg),
      currentReturnType(0) {}

Lowering::~Lowering() {
    for (std::map<int, Type*>::iterator it = builtinCache.begin();
         it != builtinCache.end(); ++it) {
        delete it->second;
    }
    for (std::size_t i = 0; i < ownedDecays.size(); ++i) delete ownedDecays[i];
}

// --- Scopes and slots ---

void Lowering::pushScope() {
    scopeMarks.push_back(static_cast<int>(scopeNames.size()));
}

void Lowering::popScope() {
    if (scopeMarks.empty()) return;
    const int mark = scopeMarks.back();
    scopeMarks.pop_back();
    while (static_cast<int>(scopeNames.size()) > mark) {
        const Shadowed &s = scopeNames.back();
        if (s.prevSlot >= 0) slots[s.name] = s.prevSlot;
        else                 slots.erase(s.name);
        if (s.prevType)      localTypes[s.name] = s.prevType;
        else                 localTypes.erase(s.name);
        scopeNames.pop_back();
    }
}

int Lowering::declareLocal(const std::string &name, int size, bool isParam, bool isFloat,
                           bool isObject) {
    const int slot = fn->addLocal(name, size, isParam, isFloat, isObject);

    // Remember what this name meant before, so the block can put it back.
    Shadowed prev;
    prev.name = name;
    std::map<std::string, int>::const_iterator os = slots.find(name);
    if (os != slots.end()) prev.prevSlot = os->second;
    std::map<std::string, Type*>::const_iterator ot = localTypes.find(name);
    if (ot != localTypes.end()) prev.prevType = ot->second;
    scopeNames.push_back(prev);

    slots[name] = slot;
    return slot;
}

int Lowering::findSlot(const std::string &name) const {
    std::map<std::string, int>::const_iterator it = slots.find(name);
    return (it == slots.end()) ? -1 : it->second;
}

int Lowering::sizeOfType(Type *t) const {
    const int s = layout.sizeOf(t);
    return s > 0 ? s : Layout::IntSize;
}

// Types the lowering pass forms for literals and for the common type of a
// binary operator.  Owned here, because they belong to no AST node.
// Lowering does pointer arithmetic on decayed types, so an array is turned
// into a pointer to its element here, exactly as the semantic pass did.
Type *Lowering::decayType(Type *t) {
    ArrayType *at = dynamic_cast<ArrayType*>(t);
    if (!at) return t;
    Type *element = cloneTypeShallow(at->element);
    if (!element) return t;             // nothing safe to point at
    Type *p = new PointerType(element);
    ownedDecays.push_back(p);
    return p;
}

// A copy the CALLER owns.  ~PointerType deletes its base, so a formed pointer
// must never be handed a node anything else owns -- an array of objects gave
// its ClassType two owners and freed it twice.
Type *Lowering::cloneTypeShallow(Type *t) {
    if (!t) return 0;
    if (BuiltinType *bt = dynamic_cast<BuiltinType*>(t)) return new BuiltinType(bt->kind);
    if (PointerType *pt = dynamic_cast<PointerType*>(t)) {
        Type *base = cloneTypeShallow(pt->base);
        return base ? new PointerType(base) : 0;
    }
    if (ArrayType *at = dynamic_cast<ArrayType*>(t)) {
        Type *element = cloneTypeShallow(at->element);
        return element ? new ArrayType(element, at->count) : 0;
    }
    return cloneForeignType(t);         // virtual: class and reference types
}

Type *Lowering::literalType(BuiltinKind k) {
    std::map<int, Type*>::iterator it = builtinCache.find(static_cast<int>(k));
    if (it != builtinCache.end()) return it->second;
    Type *t = new BuiltinType(k);
    builtinCache[static_cast<int>(k)] = t;
    return t;
}

Type *Lowering::commonType(BuiltinKind k) { return literalType(k); }

// The same rule the semantic pass applied, restated where lowering needs it.
BuiltinKind Lowering::commonKind(BuiltinKind a, BuiltinKind b) {
    if (a == BK_Double || b == BK_Double) return BK_Double;
    if (a == BK_Float  || b == BK_Float)  return BK_Float;
    if (builtinRank(a) < builtinRank(BK_Int)) a = BK_Int;
    if (builtinRank(b) < builtinRank(BK_Int)) b = BK_Int;
    if (a == b) return a;
    const int ra = builtinRank(a), rb = builtinRank(b);
    if (ra != rb) return (ra > rb) ? a : b;
    return builtinIsSigned(a) ? b : a;
}

bool Lowering::isArrayType(Type *t) {
    return dynamic_cast<ArrayType*>(t) != 0;
}

bool Lowering::isFloatType(Type *t) {
    BuiltinKind k;
    return arithKind(t, k) && builtinIsFloating(k);
}

bool Lowering::arithKind(Type *t, BuiltinKind &out) {
    BuiltinType *bt = dynamic_cast<BuiltinType*>(t);
    if (!bt || !builtinIsArithmetic(bt->kind)) return false;
    out = bt->kind;
    return true;
}

// A conversion is never free: a narrower integer must be truncated, a wider one
// sign- or zero-extended, and int and float do not even share a register file
// on most machines.  Emitting them explicitly is what stops a size mismatch
// slipping silently into the code generator.
IRReg Lowering::convert(IRReg value, Type *from, Type *to, int line) {
    // Converting TO bool is a test against zero, whatever the source -- an
    // integer, a floating value or a pointer.  That is the one conversion in
    // the language that is a comparison rather than a resize.
    if (isBoolType(to)) {
        if (isBoolType(from)) return value;
        if (isFloatType(from)) {
            const IRReg zero = fn->emitFConst(0.0, line);
            return fn->emitBinary(IR_FCmpNE, value, zero, line);
        }
        const IRReg zero = fn->emitConst(0, line);
        return fn->emitBinary(IR_CmpNE, value, zero, line);
    }
    // Converting FROM bool: the value is already 0 or 1, so only its width
    // may need adjusting.
    if (isBoolType(from)) {
        BuiltinKind k;
        if (!arithKind(to, k)) return value;
        if (builtinIsFloating(k)) {
            return fn->emitConvert(IR_IntToFloat, value, 1, IR_NoReg, line);
        }
        return fn->emitConvert(IR_IntResize, value, builtinSize(k), 0, line);
    }

    BuiltinKind kf, kt;
    if (!arithKind(from, kf) || !arithKind(to, kt)) return value;
    if (kf == kt) return value;

    const bool ff = builtinIsFloating(kf);
    const bool ft = builtinIsFloating(kt);

    if (ff && ft) {
        return fn->emitConvert(IR_FloatResize, value, builtinSize(kt), IR_NoReg, line);
    }
    if (!ff && ft) {
        // Integer to floating; the source's signedness decides the instruction.
        return fn->emitConvert(IR_IntToFloat, value,
                               builtinIsSigned(kf) ? 0 : 1, IR_NoReg, line);
    }
    if (ff && !ft) {
        const IRReg asInt = fn->emitConvert(IR_FloatToInt, value,
                                            builtinSize(kt), IR_NoReg, line);
        return asInt;
    }
    // Integer to integer: resize, sign-extending only from a signed source.
    return fn->emitConvert(IR_IntResize, value, builtinSize(kt),
                           builtinIsSigned(kf) ? 1 : 0, line);
}

// --- types, recomputed cheaply ---------------------------------------

Type *Lowering::typeOf(Expr *e) {
    if (!e) return 0;
    // A name keeps its DECLARED type.  Semantic reports what an expression
    // sees -- an array decayed to a pointer, a reference already stripped --
    // but lowering has to know the storage before it can address it.
    if (IdentExpr *id = dynamic_cast<IdentExpr*>(e)) {
        std::map<std::string, Type*>::iterator it = localTypes.find(id->name);
        if (it != localTypes.end()) return it->second;
        it = globalTypes.find(id->name);
        if (it != globalTypes.end()) return it->second;
        return e->resolvedType;
    }
    // *p likewise: Semantic decays the inner array of g[1][2] to a pointer,
    // and lowering would then load an address out of the array's own bytes.
    if (UnaryExpr *ud = dynamic_cast<UnaryExpr*>(e)) {
        if (ud->op == UN_Deref) {
            Type *base = decayType(typeOf(ud->operand));
            if (PointerType *pt = dynamic_cast<PointerType*>(base)) return pt->base;
            return e->resolvedType;
        }
    }
    // Everywhere else Semantic's answer is the complete one; what follows is
    // the fallback for a node the analysis never reached.
    if (e->resolvedType) return e->resolvedType;
    if (NumberExpr *n = dynamic_cast<NumberExpr*>(e)) return literalType(n->kind);
    if (FloatExpr *f = dynamic_cast<FloatExpr*>(e))   return literalType(f->kind);
    if (UnaryExpr *u = dynamic_cast<UnaryExpr*>(e)) {
        // ++x and x-- have the type of what they step, which is what keeps a
        // double out of the integer opcodes.
        if (u->op == UN_Neg || unaryOpIsIncDec(u->op)) return typeOf(u->operand);
        if (u->op == UN_Deref) {
            Type *base = decayType(typeOf(u->operand));
            if (PointerType *pt = dynamic_cast<PointerType*>(base)) return pt->base;
        }
        return 0;                                   // &x, !x
    }
    if (CastExpr *c = dynamic_cast<CastExpr*>(e)) return c->type;
    // A call has the type its function returns.  Without this a double-valued
    // function handed to an int parameter arrives as raw bits.
    if (CallExpr *call = dynamic_cast<CallExpr*>(e)) {
        if (call->resolved) return call->resolved->retType;
        if (IdentExpr *cid = dynamic_cast<IdentExpr*>(call->callee)) {
            std::map<std::string, Function*>::const_iterator it = functions.find(cid->name);
            if (it != functions.end()) return it->second->retType;
        }
        return 0;
    }
    if (BinaryExpr *b = dynamic_cast<BinaryExpr*>(e)) {
        if (binaryOpIsAssignment(b->op)) return typeOf(b->lhs);
        // p + n and p - n stay pointers, which is what makes a[i] load the
        // right width.
        if (b->op == BIN_Add || b->op == BIN_Sub) {
            Type *lt = decayType(typeOf(b->lhs));
            if (dynamic_cast<PointerType*>(lt)) return lt;
            Type *rt = decayType(typeOf(b->rhs));
            if (b->op == BIN_Add && dynamic_cast<PointerType*>(rt)) return rt;
        }
        // Arithmetic yields the type its operands met in.  Without this a
        // nested expression loses its type and the next operator falls back to
        // integer -- so 3.14 * r * r would multiply with the wrong opcode.
        if (!binaryOpIsComparison(b->op) && !binaryOpIsLogical(b->op)) {
            BuiltinKind kl, kr;
            if (arithKind(typeOf(b->lhs), kl) && arithKind(typeOf(b->rhs), kr)) {
                return literalType(commonKind(kl, kr));
            }
        }
        return 0;
    }
    return 0;
}

bool Lowering::isReferenceExpr(Expr *) {
    return false;               // C has no references
}

bool Lowering::isReferenceType(Type *) {
    return false;
}

Type *Lowering::referentType(Type *t) {
    return t;                   // C has no references
}

bool Lowering::isObjectType(Type *) {
    return false;               // C has no class types
}

void Lowering::reassertVPtr(Type *, IRReg, int) {}      // C has no vptr

bool Lowering::yieldsObject(Expr *) const { return false; }   // C has no objects

const char *Lowering::ReturnSlotName = "$ret";   // '$' keeps it out of reach of user code

bool Lowering::returnsObject(Function *f) {
    return f && isObjectType(f->retType);
}

IRReg Lowering::takeObjectDest() {
    const IRReg d = objectDest;
    objectDest = IR_NoReg;      // at most once: the outermost expression gets it
    return d;
}

IRReg Lowering::allocReturnSlot(Function *target, int line, IRReg given) {
    // Given a destination, the callee writes the result straight into it: no
    // temporary to copy out of, and none to destroy afterwards.
    if (given != IR_NoReg) return given;

    const int size = sizeOfType(target->retType);
    const int slot = declareLocal("$result", size > 0 ? size : Layout::PointerSize, false);
    return fn->emitLocalAddr(slot, line);
}

// The C layer has no constructors: a returned object is its bytes.
void Lowering::emitReturnObject(IRReg dest, Expr *e, int line) {
    fn->emitMemCopy(dest, lowerObjectValue(e), sizeOfType(currentReturnType), line);
}

IRReg Lowering::lowerByValueObject(Type *, Expr *e, int) {
    return lowerObjectValue(e);         // C has no copy constructor to call
}

void Lowering::destroyArgTempsDownTo(std::size_t mark, int) {
    while (argTemps.size() > mark) argTemps.pop_back();   // C has none to destroy
}

IRReg Lowering::lowerObjectValue(Expr *e) {
    // A call already yields the address of the caller-supplied slot; a
    // temporary yields the space it was built in; anything else with a place
    // in memory yields that place.
    if (dynamic_cast<CallExpr*>(e)) return lowerValue(e);
    if (yieldsObject(e)) return lowerValue(e);
    if (BinaryExpr *b = dynamic_cast<BinaryExpr*>(e)) {
        if (b->resolvedOperator) return lowerValue(e);
    }
    return lowerAddress(e);
}

// --- Declarations ---

void Lowering::lowerUnit(const std::vector<Decl*> &units) {
    // Globals first, so a function body can refer to any of them.
    for (std::size_t i = 0; i < units.size(); ++i) {
        VarDecl *vd = dynamic_cast<VarDecl*>(units[i]);
        if (vd) {
            mod.globals.push_back(IRGlobal(vd->name, sizeOfType(vd->type)));
            globalTypes[vd->name] = vd->type;
        }
    }
    // Record every function first, bodiless declarations included: an argument
    // must be converted to its parameter's type, and a native is declared and
    // never defined.
    for (std::size_t i = 0; i < units.size(); ++i) {
        Function *f = dynamic_cast<Function*>(units[i]);
        if (f) functions[f->name] = f;
    }
    for (std::size_t i = 0; i < units.size(); ++i) lowerDecl(units[i]);
    emitGlobalInit(units);
    emitGlobalFini(units);
}

const char *Lowering::GlobalInitName = "__global_init";
const char *Lowering::GlobalFiniName = "__global_fini";

// Everything a global needs before main runs: scalar initialisers, and (in the
// layer above) constructors for global objects.  One function, called once.
void Lowering::emitGlobalInit(const std::vector<Decl*> &units) {
    IRFunction *irf = new IRFunction(GlobalInitName, GlobalInitName);
    mod.functions.push_back(irf);

    IRFunction *savedFn = fn;
    Type *savedReturn = currentReturnType;
    fn = irf;
    currentReturnType = 0;

    int line = 0;
    for (std::size_t i = 0; i < units.size(); ++i) {
        VarDecl *vd = dynamic_cast<VarDecl*>(units[i]);
        if (!vd) continue;
        line = vd->line;
        initGlobal(vd, fn->emitGlobalAddr(vd->name, vd->line));
    }
    fn->emitReturn(IR_NoReg, line);

    fn = savedFn;
    currentReturnType = savedReturn;
}

// The mirror of emitGlobalInit: reverse order, as every other scope exit is.
void Lowering::emitGlobalFini(const std::vector<Decl*> &units) {
    IRFunction *irf = new IRFunction(GlobalFiniName, GlobalFiniName);
    mod.functions.push_back(irf);

    IRFunction *savedFn = fn;
    Type *savedReturn = currentReturnType;
    fn = irf;
    currentReturnType = 0;

    int line = 0;
    for (std::size_t i = units.size(); i > 0; --i) {
        VarDecl *vd = dynamic_cast<VarDecl*>(units[i - 1]);
        if (!vd) continue;
        line = vd->line;
        destroyGlobal(vd);
    }
    fn->emitReturn(IR_NoReg, line);

    fn = savedFn;
    currentReturnType = savedReturn;
}

void Lowering::destroyGlobal(VarDecl *) {}              // C has no destructors

void Lowering::initGlobal(VarDecl *vd, IRReg addr) {
    if (!vd->init) return;
    Type *t = referentType(vd->type);
    IRReg v = lowerValue(vd->init);
    v = convert(v, referentType(typeOf(vd->init)), t, vd->line);
    fn->emitStore(addr, v, sizeOfType(t), isFloatType(t), vd->line);
}

// A native keeps its plain name so the VM can recognise it; everything else
// carries its signature, because a name alone no longer identifies a function.
std::string Lowering::symbolFor(Function *f, const std::string &className) {
    if (className.empty()) {
        // main is the entry point and cannot be overloaded, so it keeps its
        // plain name -- as it does in a real toolchain.
        if (f->name == "main") return f->name;
        // A native is recognised by name, so it keeps its own too.
        if (!f->body && nativeByName(f->name) != NAT_Count) return f->name;
    }
    return mangleOverload(className, f->name, f->params);
}

void Lowering::lowerDecl(Decl *d) {
    Function *f = dynamic_cast<Function*>(d);
    if (f && f->body) lowerFunction(f, symbolFor(f, ""), f->name, false);
}

void Lowering::lowerFunction(Function *f, const std::string &mangled,
                             const std::string &sourceName, bool hasThis) {
    IRFunction *irf = new IRFunction(mangled, sourceName);
    mod.functions.push_back(irf);

    IRFunction *savedFn = fn;
    Type *savedReturn = currentReturnType;
    fn = irf;
    currentReturnType = f->retType;
    // A fresh naming environment: nothing from the caller's scope is visible.
    std::map<std::string, int> savedSlots;
    savedSlots.swap(slots);
    std::map<std::string, Type*> savedTypes;
    savedTypes.swap(localTypes);

    pushScope();

    // An ordinary first parameter, once the C++ is erased.
    if (hasThis) {
        declareLocal("this", Layout::PointerSize, true);
        ++irf->paramCount;
    }
    // Then the hidden result pointer, if the function returns an object.
    if (returnsObject(f)) {
        declareLocal(ReturnSlotName, Layout::PointerSize, true);
        ++irf->paramCount;
    }
    for (std::size_t i = 0; i < f->params.size(); ++i) {
        VarDecl *p = f->params[i];
        const std::string pname = p->name.empty() ? "_" : p->name;
        declareLocal(pname, sizeOfType(p->type), true, isFloatType(referentType(p->type)),
                     isObjectType(p->type));
        localTypes[pname] = p->type;
        ++irf->paramCount;
    }
    irf->returnsValue = (f->retType != 0);

    // Globals are initialised before the first statement of main, which is
    // where "before the program runs" actually means something.
    if (mangled == "main") {
        std::vector<IRReg> none;
        irf->emitCall(GlobalInitName, none, false, f->line);
    }

    emitPrologue(f);                        // virtual: a constructor's preamble
    if (f->body) lowerBlock(f->body);
    // A body that already returned emitted its tail on the path that left.
    if (!irf->endsWithTerminator()) {
        emitEpilogue(f);                    // virtual: a destructor's tail
        irf->emitReturn(IR_NoReg, f->line);
    }

    popScope();
    fn = savedFn;
    currentReturnType = savedReturn;
    slots.swap(savedSlots);
    localTypes.swap(savedTypes);
}

void Lowering::emitPrologue(Function *) {}
void Lowering::emitEpilogue(Function *) {}
void Lowering::emitScopeExit(CompoundStmt *) {}
void Lowering::emitAllOpenScopeExits() {}
void Lowering::emitScopeExitsDownTo(std::size_t) {}

// --- Statements ---

void Lowering::lowerBlock(CompoundStmt *block) {
    pushScope();
    openBlocks.push_back(block);
    for (std::size_t i = 0; i < block->body.size(); ++i) lowerStmt(block->body[i]);
    // If the block already returned, its destructors ran on that path.
    if (!fn->endsWithTerminator()) emitScopeExit(block);
    openBlocks.pop_back();
    popScope();
}

void Lowering::lowerStmt(Stmt *s) {
    if (!s) return;

    if (CompoundStmt *b = dynamic_cast<CompoundStmt*>(s)) { lowerBlock(b); return; }
    if (DeclStmt *ds = dynamic_cast<DeclStmt*>(s))        { lowerVarDecl(ds->var); return; }

    if (ExprStmt *es = dynamic_cast<ExprStmt*>(s)) {
        // The value is discarded, but the call still happens.
        if (CallExpr *call = dynamic_cast<CallExpr*>(es->expr)) lowerCall(call, false);
        else if (es->expr) lowerValue(es->expr);
        return;
    }

    if (ReturnStmt *rs = dynamic_cast<ReturnStmt*>(s)) {
        IRReg v = IR_NoReg;
        if (rs->expr && isObjectType(currentReturnType)) {
            // Copy into the caller's slot BEFORE the destructors run: the
            // object being returned is one of the locals about to be destroyed.
            const int slot = findSlot(ReturnSlotName);
            if (slot >= 0) {
                const IRReg dest = fn->emitLoad(fn->emitLocalAddr(slot, rs->line),
                                                Layout::PointerSize, false, rs->line);
                emitReturnObject(dest, rs->expr, rs->line);
                v = dest;
            }
        } else if (rs->expr && isReferenceType(currentReturnType)) {
            // T& hands back the ADDRESS of what it names -- that is the whole
            // of what a reference return is, and what makes  t[1] = 42;  work.
            v = lowerAddress(rs->expr);
        } else if (rs->expr) {
            v = lowerValue(rs->expr);
            v = convert(v, typeOf(rs->expr), currentReturnType, rs->line);
        }
        // Everything this return leaves is torn down first.
        emitAllOpenScopeExits();
        fn->emitReturn(v, rs->line);
        return;
    }

    if (IfStmt *is = dynamic_cast<IfStmt*>(s))    { lowerIf(is); return; }
    if (DoWhileStmt *dw = dynamic_cast<DoWhileStmt*>(s)) { lowerDoWhile(dw); return; }
    if (SwitchStmt *sw = dynamic_cast<SwitchStmt*>(s))   { lowerSwitch(sw); return; }
    if (CaseStmt *cs = dynamic_cast<CaseStmt*>(s)) {
        // A case label is exactly that: a place to jump to.
        std::map<const CaseStmt*, int>::const_iterator it = caseLabels.find(cs);
        if (it != caseLabels.end()) fn->emitLabel(it->second);
        return;
    }
    if (WhileStmt *ws = dynamic_cast<WhileStmt*>(s)) { lowerWhile(ws); return; }
    if (ForStmt *fs = dynamic_cast<ForStmt*>(s))  { lowerFor(fs); return; }

    if (dynamic_cast<BreakStmt*>(s)) {
        if (!breakTargets.empty()) {
            emitScopeExitsDownTo(breakScopeDepth.back());
            fn->emitJump(breakTargets.back(), s->line);
        }
        return;
    }
    if (dynamic_cast<ContinueStmt*>(s)) {
        if (!continueTargets.empty()) {
            emitScopeExitsDownTo(continueScopeDepth.back());
            fn->emitJump(continueTargets.back(), s->line);
        }
        return;
    }
}

void Lowering::lowerVarDecl(VarDecl *vd) {
    if (!vd) return;
    const int size = sizeOfType(vd->type);
    const int slot = declareLocal(vd->name, size, false, isFloatType(referentType(vd->type)));
    localTypes[vd->name] = vd->type;
    if (vd->init) {
        // A reference stores the ADDRESS of what it binds to -- the whole of
        // what a reference becomes.  The DECLARED type decides that, not the
        // initialiser: `Base& r = obj;` binds to obj, it does not copy it.
        IRReg value;
        if (isReferenceType(vd->type)) {
            value = lowerAddress(vd->init);
        } else {
            value = lowerValue(vd->init);
            value = convert(value, typeOf(vd->init), vd->type, vd->line);
        }
        const IRReg addr = fn->emitLocalAddr(slot, vd->line);
        fn->emitStore(addr, value, size, isFloatType(vd->type), vd->line);
    }
}

void Lowering::lowerIf(IfStmt *s) {
    const int elseLabel = fn->newLabel();
    const int endLabel = s->elseBranch ? fn->newLabel() : elseLabel;

    const IRReg cond = lowerValue(s->cond);
    fn->emitBranchZero(cond, elseLabel, s->line);
    lowerStmt(s->thenBranch);
    if (s->elseBranch) {
        fn->emitJump(endLabel, s->line);
        fn->emitLabel(elseLabel);
        lowerStmt(s->elseBranch);
        fn->emitLabel(endLabel);
    } else {
        fn->emitLabel(elseLabel);
    }
}

// The body runs before the condition is first tested, which is the whole of
// the difference from `while`.
void Lowering::lowerDoWhile(DoWhileStmt *s) {
    const int top = fn->newLabel();
    const int test = fn->newLabel();
    const int done = fn->newLabel();

    fn->emitLabel(top);
    breakTargets.push_back(done);
    breakScopeDepth.push_back(openBlocks.size());
    continueTargets.push_back(test);                        // `continue` goes to the test
    continueScopeDepth.push_back(openBlocks.size());
    lowerStmt(s->body);
    continueTargets.pop_back();
    continueScopeDepth.pop_back();
    breakTargets.pop_back();
    breakScopeDepth.pop_back();

    fn->emitLabel(test);
    const IRReg cond = lowerValue(s->cond);
    fn->emitBranchNZ(cond, top, s->line);
    fn->emitLabel(done);
}

// A comparison chain, then the body emitted straight through -- so control
// enters at the matching label and runs on until a break, which is what
// fall-through is.  A jump table would be faster and would hide that.
void Lowering::lowerSwitch(SwitchStmt *s) {
    const int done = fn->newLabel();
    int defaultLabel = done;

    std::map<const CaseStmt*, int> saved;
    saved.swap(caseLabels);

    const IRReg subject = lowerValue(s->cond);

    std::vector<const CaseStmt*> cases;
    if (s->body) {
        for (std::size_t i = 0; i < s->body->body.size(); ++i) {
            CaseStmt *c = dynamic_cast<CaseStmt*>(s->body->body[i]);
            if (!c) continue;
            const int label = fn->newLabel();
            caseLabels[c] = label;
            if (c->isDefault) defaultLabel = label;
            else              cases.push_back(c);
        }
    }

    for (std::size_t i = 0; i < cases.size(); ++i) {
        const IRReg want = fn->emitConst(cases[i]->value, s->line);
        const IRReg eq = fn->emitBinary(IR_CmpEQ, subject, want, s->line);
        fn->emitBranchNZ(eq, caseLabels[cases[i]], s->line);
    }
    fn->emitJump(defaultLabel, s->line);

    breakTargets.push_back(done);
    breakScopeDepth.push_back(openBlocks.size());
    lowerStmt(s->body);
    breakTargets.pop_back();
    breakScopeDepth.pop_back();
    fn->emitLabel(done);

    caseLabels.swap(saved);
}

void Lowering::lowerWhile(WhileStmt *s) {
    const int top = fn->newLabel();
    const int done = fn->newLabel();

    fn->emitLabel(top);
    const IRReg cond = lowerValue(s->cond);
    fn->emitBranchZero(cond, done, s->line);

    breakTargets.push_back(done);
    breakScopeDepth.push_back(openBlocks.size());
    continueTargets.push_back(top);
    continueScopeDepth.push_back(openBlocks.size());
    lowerStmt(s->body);
    continueTargets.pop_back();
    continueScopeDepth.pop_back();
    breakTargets.pop_back();
    breakScopeDepth.pop_back();

    fn->emitJump(top, s->line);
    fn->emitLabel(done);
}

void Lowering::lowerFor(ForStmt *s) {
    // The init declaration belongs to the loop.
    pushScope();
    if (s->init) lowerStmt(s->init);

    const int top = fn->newLabel();
    const int step = fn->newLabel();
    const int done = fn->newLabel();

    fn->emitLabel(top);
    if (s->cond) {
        const IRReg cond = lowerValue(s->cond);
        fn->emitBranchZero(cond, done, s->line);
    }

    breakTargets.push_back(done);
    breakScopeDepth.push_back(openBlocks.size());
    continueTargets.push_back(step);                        // continue runs the step first
    continueScopeDepth.push_back(openBlocks.size());
    lowerStmt(s->body);
    continueTargets.pop_back();
    continueScopeDepth.pop_back();
    breakTargets.pop_back();
    breakScopeDepth.pop_back();

    fn->emitLabel(step);
    if (s->step) lowerValue(s->step);
    fn->emitJump(top, s->line);
    fn->emitLabel(done);
    popScope();
}

// --- Expressions ---

IRReg Lowering::lowerAddress(Expr *e) {
    if (!e) return IR_NoReg;

    IRReg out = IR_NoReg;
    if (lowerLayerAddress(e, out)) return out;      // virtual: a.b, this, ...

    if (IdentExpr *id = dynamic_cast<IdentExpr*>(e)) {
        const int slot = findSlot(id->name);
        if (slot >= 0) {
            const IRReg addr = fn->emitLocalAddr(slot, e->line);
            // A reference's slot holds another object's address, so the address
            // OF the reference is the value IN its slot.
            if (isReferenceExpr(e)) return fn->emitLoad(addr, Layout::PointerSize, false, e->line);
            return addr;
        }
        return fn->emitGlobalAddr(id->name, e->line);
    }

    if (UnaryExpr *u = dynamic_cast<UnaryExpr*>(e)) {
        if (u->op == UN_Deref) return lowerValue(u->operand);   // *p: p's value
    }

    if (IndexExpr *ix = dynamic_cast<IndexExpr*>(e)) return lowerIndexAddress(ix);

    // A call, or an overloaded operator, whose result is an object: it has no
    // name, but it does have a place -- the slot the caller supplied for it.
    // That is what makes  (a + b).x  addressable.
    if (CallExpr *c = dynamic_cast<CallExpr*>(e)) {
        // A call returning T& already yields an address; one returning an
        // object yields the slot the caller supplied for it.
        if (c->resolved && (isObjectType(c->resolved->retType) ||
                            isReferenceType(c->resolved->retType))) {
            return lowerValue(e);
        }
    }
    if (BinaryExpr *bo = dynamic_cast<BinaryExpr*>(e)) {
        if (bo->resolvedOperator && isObjectType(bo->resolvedOperator->retType)) {
            return lowerValue(e);
        }
    }
    if (UnaryExpr *uo = dynamic_cast<UnaryExpr*>(e)) {
        if (uo->resolvedOperator && isObjectType(uo->resolvedOperator->retType)) {
            return lowerValue(e);
        }
    }

    if (BinaryExpr *b = dynamic_cast<BinaryExpr*>(e)) {
        // An assignment is an lvalue; its address is the left side's.
        if (binaryOpIsAssignment(b->op)) { lowerAssign(b); return lowerAddress(b->lhs); }
    }

    diag.error(e->line, e->col, "internal: expression has no address to lower");
    return fn->emitConst(0, e->line);
}

IRReg Lowering::lowerValue(Expr *e) {
    if (!e) return IR_NoReg;

    IRReg out = IR_NoReg;
    if (lowerLayerValue(e, out)) return out;        // virtual: this, new, a.b

    if (NumberExpr *n = dynamic_cast<NumberExpr*>(e)) {
        return fn->emitConst(n->value, e->line);
    }
    if (FloatExpr *f = dynamic_cast<FloatExpr*>(e)) {
        return fn->emitFConst(f->value, e->line);
    }
    if (StringExpr *str = dynamic_cast<StringExpr*>(e)) {
        return fn->emitStringAddr(mod.internString(str->value), e->line);
    }

    if (IdentExpr *id = dynamic_cast<IdentExpr*>(e)) {
        const IRReg addr = lowerAddress(e);
        Type *t = referentType(typeOf(e));
        (void)id;
        // An array decays: its value IS its address, with nothing loaded.
        if (isArrayType(t)) return addr;
        return fn->emitLoad(addr, t ? sizeOfType(t) : Layout::IntSize,
                            isFloatType(t), e->line);
    }

    if (IndexExpr *ix = dynamic_cast<IndexExpr*>(e)) {
        // An overload that returns by VALUE has no address to load from.
        if (ix->resolvedOperator && !isReferenceType(ix->resolvedOperator->retType)) {
            return lowerIndexOperator(ix);
        }
        const IRReg addr = lowerIndexAddress(ix);
        Type *t = ix->resolvedOperator ? referentType(typeOf(e)) : elementTypeOf(ix);
        if (!t) t = referentType(typeOf(e));
        // An array element that is itself an array, or an object, has no value
        // to load: its address IS the value.
        if (isArrayType(t) || isObjectType(t)) return addr;
        return fn->emitLoad(addr, t ? sizeOfType(t) : Layout::IntSize,
                            isFloatType(t), e->line);
    }

    if (CastExpr *ce = dynamic_cast<CastExpr*>(e)) {
        // A cast is an explicit conversion; the same machinery serves.
        const IRReg v = lowerValue(ce->expr);
        return convert(v, typeOf(ce->expr), ce->type, e->line);
    }
    if (CallExpr *call = dynamic_cast<CallExpr*>(e)) return lowerCall(call, true);
    if (UnaryExpr *u = dynamic_cast<UnaryExpr*>(e))  return lowerUnary(u);
    if (BinaryExpr *b = dynamic_cast<BinaryExpr*>(e)) return lowerBinary(b);

    diag.error(e->line, e->col, "internal: unhandled expression in lowering");
    return fn->emitConst(0, e->line);
}

// a[i] is base + i * sizeof(element) -- the same arithmetic the desugared
// *(a + i) used to produce, so the emitted IR is unchanged.  A class that
// overloads it takes the other branch, and the call IS the address when the
// overload returns a reference.
Type *Lowering::elementTypeOf(IndexExpr *e) {
    Type *bt = decayType(referentType(typeOf(e->base)));
    PointerType *pt = dynamic_cast<PointerType*>(bt);
    return pt ? pt->base : 0;
}

IRReg Lowering::lowerIndexAddress(IndexExpr *e) {
    if (e->resolvedOperator) return lowerIndexOperator(e);

    Type *bt = decayType(referentType(typeOf(e->base)));
    const IRReg base = lowerValue(e->base);
    IRReg index = lowerValue(e->index);

    PointerType *pt = dynamic_cast<PointerType*>(bt);
    const int step = pt ? sizeOfType(pt->base) : 1;
    if (step > 1) {
        index = fn->emitBinary(IR_Mul, index, fn->emitConst(step, e->line), e->line);
    }
    return fn->emitBinary(IR_Add, base, index, e->line);
}

IRReg Lowering::lowerIndexOperator(IndexExpr *e) {
    diag.error(e->line, e->col, "internal: operator[] outside the C++ layer");
    return fn->emitConst(0, e->line);
}

// ++p on a pointer moves by one object, not one byte.
IRReg Lowering::stepFor(Type *t, int line) {
    PointerType *pt = dynamic_cast<PointerType*>(t);
    return fn->emitConst(pt ? sizeOfType(pt->base) : 1, line);
}

// The target's address is taken ONCE and reused for the load and the store.
// Prefix yields the new value, postfix the old one; nothing else differs.
IRReg Lowering::lowerIncDec(UnaryExpr *e) {
    Type *t = referentType(typeOf(e->operand));
    const int size = t ? sizeOfType(t) : Layout::IntSize;
    const bool flt = isFloatType(t);

    const IRReg addr = lowerAddress(e->operand);
    const IRReg oldValue = fn->emitLoad(addr, size, flt, e->line);
    const IRReg step = stepFor(t, e->line);
    const bool up = (e->op == UN_PreInc || e->op == UN_PostInc);

    IRReg newValue;
    if (flt) {
        const IRReg fstep = fn->emitConvert(IR_IntToFloat, step, 0, IR_NoReg, e->line);
        newValue = fn->emitBinary(up ? IR_FAdd : IR_FSub, oldValue, fstep, e->line);
    } else {
        newValue = fn->emitBinary(up ? IR_Add : IR_Sub, oldValue, step, e->line);
    }
    fn->emitStore(addr, newValue, size, flt, e->line);
    return (e->op == UN_PreInc || e->op == UN_PreDec) ? newValue : oldValue;
}

IRReg Lowering::lowerUnary(UnaryExpr *e) {
    if (unaryOpIsIncDec(e->op)) return lowerIncDec(e);
    switch (e->op) {
    case UN_Neg: {
        BuiltinKind k;
        const bool flt = arithKind(referentType(typeOf(e->operand)), k) && builtinIsFloating(k);
        return fn->emitUnary(flt ? IR_FNeg : IR_Neg, lowerValue(e->operand), e->line);
    }
    case UN_Not:    return fn->emitUnary(IR_LogicalNot,
                        truth(lowerValue(e->operand), referentType(typeOf(e->operand)), e->line),
                        e->line);
    case UN_AddrOf: return lowerAddress(e->operand);        // &x IS the address
    case UN_Deref: {
        const IRReg addr = lowerValue(e->operand);
        Type *t = typeOf(e);
        // *p on a pointer-to-array yields the array's address; there is
        // nothing to load, because an array is not a register-sized value.
        if (isArrayType(t)) return addr;
        return fn->emitLoad(addr, t ? sizeOfType(t) : Layout::IntSize,
                            isFloatType(t), e->line);
    }
    default:
        break;
    }
    return IR_NoReg;
}

// Both operands are converted to the type they meet in before the operator
// runs, and the operator chosen depends on that type: integer, unsigned and
// floating arithmetic are three different machine operations.
IRReg Lowering::lowerBinary(BinaryExpr *e) {
    if (binaryOpIsAssignment(e->op)) return lowerAssign(e);
    if (e->op == BIN_LAnd || e->op == BIN_LOr) return lowerShortCircuit(e);

    Type *lt = referentType(typeOf(e->lhs));
    Type *rt = referentType(typeOf(e->rhs));
    IRReg a = lowerValue(e->lhs);
    IRReg b = lowerValue(e->rhs);

    // Pointer arithmetic counts objects, not bytes, so the integer side is
    // scaled by the pointee's size before the add.  This is the whole of what
    // makes a[i] reach element i rather than byte i.
    lt = decayType(lt);
    rt = decayType(rt);
    PointerType *pl = dynamic_cast<PointerType*>(lt);
    PointerType *pr = dynamic_cast<PointerType*>(rt);
    if ((pl || pr) && (e->op == BIN_Add || e->op == BIN_Sub)) {
        if (pl && pr) {
            // p - q: the byte difference divided by the element size.
            const IRReg diff = fn->emitBinary(IR_Sub, a, b, e->line);
            const int step = sizeOfType(pl->base);
            if (step <= 1) return diff;
            const IRReg by = fn->emitConst(step, e->line);
            return fn->emitBinary(IR_Div, diff, by, e->line);
        }
        PointerType *p = pl ? pl : pr;
        IRReg &index = pl ? b : a;
        const int step = sizeOfType(p->base);
        if (step > 1) {
            const IRReg by = fn->emitConst(step, e->line);
            index = fn->emitBinary(IR_Mul, index, by, e->line);
        }
        return fn->emitBinary(e->op == BIN_Add ? IR_Add : IR_Sub, a, b, e->line);
    }

    // Shift takes its type from the LEFT operand alone; the right one is a
    // count, not something to meet it in a common type.
    if (e->op == BIN_Shl || e->op == BIN_Shr) {
        BuiltinKind k;
        const bool uns = arithKind(lt, k) && !builtinIsSigned(k);
        const IROp op = (e->op == BIN_Shl) ? IR_Shl : (uns ? IR_UShr : IR_Shr);
        return fn->emitBinary(op, a, b, e->line);
    }

    BuiltinKind kl, kr;
    BuiltinKind common = BK_Int;
    if (arithKind(lt, kl) && arithKind(rt, kr)) {
        common = commonKind(kl, kr);
        a = convert(a, lt, commonType(common), e->line);
        b = convert(b, rt, commonType(common), e->line);
    }
    const bool flt = builtinIsFloating(common);
    const bool uns = !builtinIsSigned(common);

    IROp op = IR_Add;
    switch (e->op) {
    case BIN_Add: op = flt ? IR_FAdd : IR_Add; break;
    case BIN_Sub: op = flt ? IR_FSub : IR_Sub; break;
    case BIN_Mul: op = flt ? IR_FMul : IR_Mul; break;
    case BIN_Div: op = flt ? IR_FDiv : (uns ? IR_UDiv : IR_Div); break;
    case BIN_Mod: op = uns ? IR_UMod : IR_Mod; break;
    case BIN_EQ:  op = flt ? IR_FCmpEQ : IR_CmpEQ; break;
    case BIN_NE:  op = flt ? IR_FCmpNE : IR_CmpNE; break;
    case BIN_LT:  op = flt ? IR_FCmpLT : (uns ? IR_UCmpLT : IR_CmpLT); break;
    case BIN_GT:  op = flt ? IR_FCmpGT : (uns ? IR_UCmpGT : IR_CmpGT); break;
    case BIN_LE:  op = flt ? IR_FCmpLE : (uns ? IR_UCmpLE : IR_CmpLE); break;
    case BIN_GE:  op = flt ? IR_FCmpGE : (uns ? IR_UCmpGE : IR_CmpGE); break;
    default: break;
    }
    return fn->emitBinary(op, a, b, e->line);
}

IRReg Lowering::lowerAssign(BinaryExpr *e) {
    Type *t = referentType(typeOf(e->lhs));
    const int size = t ? sizeOfType(t) : Layout::IntSize;
    const bool flt = isFloatType(t);

    if (e->op == BIN_Assign) {
        // An object is copied byte for byte: it does not fit in a register,
        // and load-then-store would shift its bytes off the end of one.
        if (isObjectType(t)) {
            const IRReg src = lowerObjectValue(e->rhs);
            const IRReg dst = lowerAddress(e->lhs);
            fn->emitMemCopy(dst, src, size, e->line);
            reassertVPtr(t, dst, e->line);      // the copy is the target's class
            return dst;
        }
        // Right side first: the order the language leaves open, and the one
        // that keeps the address live for the shortest time.
        IRReg value = lowerValue(e->rhs);
        value = convert(value, referentType(typeOf(e->rhs)), t, e->line);
        const IRReg addr = lowerAddress(e->lhs);
        fn->emitStore(addr, value, size, flt, e->line);
        return value;
    }

    // a += b: the address is taken ONCE.  Lowering it as a = a + b would
    // evaluate a twice, which is wrong the moment a has a side effect.
    const IRReg addr = lowerAddress(e->lhs);
    const IRReg oldValue = fn->emitLoad(addr, size, flt, e->line);
    IRReg rhs = lowerValue(e->rhs);
    rhs = convert(rhs, referentType(typeOf(e->rhs)), t, e->line);

    const BinaryOp under = binaryOpUnderlying(e->op);
    IROp op = IR_Add;
    if (flt) {
        switch (under) {
        case BIN_Add: op = IR_FAdd; break;
        case BIN_Sub: op = IR_FSub; break;
        case BIN_Mul: op = IR_FMul; break;
        default:      op = IR_FDiv; break;
        }
    } else {
        BuiltinKind k;
        const bool uns = arithKind(t, k) && !builtinIsSigned(k);
        switch (under) {
        case BIN_Add: op = IR_Add; break;
        case BIN_Sub: op = IR_Sub; break;
        case BIN_Mul: op = IR_Mul; break;
        case BIN_Div: op = uns ? IR_UDiv : IR_Div; break;
        default:      op = uns ? IR_UMod : IR_Mod; break;
        }
    }
    const IRReg result = fn->emitBinary(op, oldValue, rhs, e->line);
    fn->emitStore(addr, result, size, flt, e->line);
    return result;
}

// Comparing against zero yields 0 or 1 and, for a double, collapses eight
// bytes to four -- both of which a logical operand needs.
IRReg Lowering::truth(IRReg value, Type *t, int line) {
    if (isFloatType(t)) {
        return fn->emitBinary(IR_FCmpNE, value, fn->emitFConst(0.0, line), line);
    }
    return fn->emitBinary(IR_CmpNE, value, fn->emitConst(0, line), line);
}

// The right side must not run when the left already decides -- a control-flow
// fact, so it lowers to branches rather than an operator.  The stored value is
// the truth of each side: `2 && 4` is 1, not 4.
IRReg Lowering::lowerShortCircuit(BinaryExpr *e) {
    const int slot = fn->addLocal("$sc", Layout::IntSize, false);
    const int done = fn->newLabel();

    const IRReg left = truth(lowerValue(e->lhs), referentType(typeOf(e->lhs)), e->line);
    IRReg addr = fn->emitLocalAddr(slot, e->line);
    fn->emitStore(addr, left, Layout::IntSize, false, e->line);

    if (e->op == BIN_LAnd) fn->emitBranchZero(left, done, e->line);
    else                   fn->emitBranchNZ(left, done, e->line);

    const IRReg right = truth(lowerValue(e->rhs), referentType(typeOf(e->rhs)), e->line);
    addr = fn->emitLocalAddr(slot, e->line);
    fn->emitStore(addr, right, Layout::IntSize, false, e->line);

    fn->emitLabel(done);
    addr = fn->emitLocalAddr(slot, e->line);
    return fn->emitLoad(addr, Layout::IntSize, false, e->line);
}

IRReg Lowering::lowerCall(CallExpr *e, bool wantsResult) {
    IdentExpr *callee = dynamic_cast<IdentExpr*>(e->callee);
    if (!callee) {
        diag.error(e->line, e->col, "internal: unsupported callee in lowering");
        return fn->emitConst(0, e->line);
    }
    // The semantic pass already chose which overload this is.
    Function *target = e->resolved;
    if (!target) {
        std::map<std::string, Function*>::const_iterator it = functions.find(callee->name);
        if (it != functions.end()) target = it->second;
    }
    const std::string sym = target ? symbolFor(target, "") : callee->name;
    const IRReg dest = takeObjectDest();
    const std::size_t mark = argTemps.size();
    std::vector<IRReg> args;
    if (returnsObject(target)) args.push_back(allocReturnSlot(target, e->line, dest));
    const std::vector<IRReg> rest = lowerArgs(e, target, 0);
    args.insert(args.end(), rest.begin(), rest.end());
    const IRReg out = fn->emitCall(sym, args, wantsResult, e->line);
    destroyArgTempsDownTo(mark, e->line);
    return out;
}

// Each argument is converted to its parameter's declared type.  Without this a
// double handed to an int parameter would arrive as raw bits.  `skip` is 1 when
// the callee is a method and `this` already occupies the first slot.
std::vector<IRReg> Lowering::lowerArgs(CallExpr *e, Function *target, std::size_t skip) {
    std::vector<IRReg> args;
    for (std::size_t i = 0; i < e->args.size(); ++i) {
        const std::size_t p = i + skip;
        Type *want = (target && p < target->params.size()) ? target->params[p]->type : 0;
        IRReg v;
        if (want && isReferenceType(want)) {
            // A reference parameter receives the object's address, never a
            // copy of its bytes.
            v = lowerAddress(e->args[i]);
        } else if (want && isObjectType(want)) {
            // By value: the address goes over, and the VM copies the object
            // into the parameter's own slot -- but a declared copy constructor
            // is what makes the copy, if the class wrote one.
            v = lowerByValueObject(want, e->args[i], e->line);
        } else {
            v = lowerValue(e->args[i]);
            if (want) v = convert(v, typeOf(e->args[i]), want, e->line);
        }
        args.push_back(v);
    }
    return args;
}

bool Lowering::lowerLayerValue(Expr *, IRReg &)   { return false; }
bool Lowering::lowerLayerAddress(Expr *, IRReg &) { return false; }

} // namespace cc
