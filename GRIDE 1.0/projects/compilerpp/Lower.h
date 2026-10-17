// Lower.h -- PASS 5b, LAYER 1: lowering the C layer, namespace `cc`.
//
// Walks the parts of the tree C already had and turns them into IR.  Where a
// construct might be a C++ one it asks a virtual hook, which here answers "not
// mine"; cxx::Lowering (Lower1.h) answers them.
//
// The pass turns on one distinction: ADDRESS versus VALUE.  An lvalue has an
// address and its value is a load from it; a non-lvalue has only a value.
// Assignment lowers its left side as an address and its right as a value.
// This is also what makes references disappear -- a reference variable holds
// an address, so lowering its address is a load, not a slot lookup.
//
// C++98 only.

#ifndef LOWER_H
#define LOWER_H

#include <map>
#include <string>
#include <vector>

#include "AST.h"
#include "Diagnostics.h"
#include "Bytecode.h"
#include "IR.h"
#include "Layout.h"

namespace cc {

class Lowering {
public:
    Lowering(IRModule &module, const Layout &layout, Diagnostics &diag);
    virtual ~Lowering();

    // Lowers a whole translation unit.
    void lowerUnit(const std::vector<Decl*> &units);

    // The synthetic function that runs every global's initialiser.  main calls
    // it first; without it `int g = 5;` left g at zero.
    static const char *GlobalInitName;
    // Its counterpart: global objects are destroyed after main returns.
    static const char *GlobalFiniName;

protected:
    IRModule &mod;
    const Layout &layout;
    Diagnostics &diag;

    IRFunction *fn;                     // the function being built, or 0
    std::map<std::string, int> slots;   // name -> frame slot, innermost wins
    // A name declared in a block may shadow one outside it, so unwinding the
    // scope has to RESTORE the outer binding rather than erase the name.
    // Erasing it made `int x; { int x; }` an internal error.
    struct Shadowed {
        std::string name;
        int   prevSlot;         // -1 when the name was not bound before
        Type *prevType;         // 0 likewise; not owned
        Shadowed() : prevSlot(-1), prevType(0) {}
    };
    std::vector<Shadowed> scopeNames;
    std::vector<int> scopeMarks;
    // Labels to jump to for `break` and `continue`, innermost last.
    std::vector<int> breakTargets;
    std::vector<int> continueTargets;
    // How many blocks were open when each loop was entered.  A break or a
    // continue leaves every block opened since, and leaving a block runs its
    // destructors -- exactly as a return does.
    std::vector<std::size_t> breakScopeDepth;
    std::vector<std::size_t> continueScopeDepth;
    // Case labels of the switch being lowered, filled on a first pass so the
    // comparison chain can be emitted before the body.
    std::map<const CaseStmt*, int> caseLabels;
    // Innermost last.  A `return` inside nested scopes runs the destructors of
    // every one of them, walking this list.
    std::vector<CompoundStmt*> openBlocks;

    // --- declarations -------------------------------------------------
    virtual void lowerDecl(Decl *d);
    std::string symbolFor(Function *f, const std::string &className);
    void lowerFunction(Function *f, const std::string &mangled,
                       const std::string &sourceName, bool hasThis);
    virtual void emitPrologue(Function *f);     // a ctor's base call and inits
    virtual void emitEpilogue(Function *f);     // a dtor's tail

    // --- statements ---------------------------------------------------
    virtual void lowerStmt(Stmt *s);
    void lowerBlock(CompoundStmt *block);
    void lowerIf(IfStmt *s);
    void lowerDoWhile(DoWhileStmt *s);
    void lowerSwitch(SwitchStmt *s);
    void lowerWhile(WhileStmt *s);
    void lowerFor(ForStmt *s);
    virtual void lowerVarDecl(VarDecl *vd);
    // Nothing in the C layer has a destructor, so these do nothing here.
    virtual void emitScopeExit(CompoundStmt *block);
    virtual void emitAllOpenScopeExits();
    // Scope exits for the blocks opened since `depth`, innermost first.
    virtual void emitScopeExitsDownTo(std::size_t depth);

    // --- expressions --------------------------------------------------
    IRReg lowerValue(Expr *e);
    IRReg lowerAddress(Expr *e);
    IRReg lowerBinary(BinaryExpr *e);
    // a[i]: the element's address, or the call when a class overloads it.
    IRReg lowerIndexAddress(IndexExpr *e);
    // What a[i] yields, taken from the BASE rather than from the analysis:
    // Semantic decays the inner array of g[1][2] to a pointer, and lowering
    // has to know it is still an array before it decides whether to load.
    Type *elementTypeOf(IndexExpr *e);
    virtual IRReg lowerIndexOperator(IndexExpr *e);      // 0 unless overloaded
    // Emits whatever machine operation the conversion needs, or nothing when
    // the two types already agree.  Every implicit conversion the semantic
    // pass allowed becomes a real instruction here.
    IRReg convert(IRReg value, Type *from, Type *to, int line);
    // The type an expression's operands meet in, so both sides are converted
    // before the operator runs.
    static bool arithKind(Type *t, BuiltinKind &out);
    static bool isFloatType(Type *t);
    // An array's VALUE is the address of its first element -- there is no
    // load, because an array is not something a register can hold.
    static bool isArrayType(Type *t);
    // bool lives in the C++ layer, so recognising and producing it are that
    // layer's job; the C layer only needs to ask.
    virtual bool isBoolType(Type *t) { (void)t; return false; }
    static BuiltinKind commonKind(BuiltinKind a, BuiltinKind b);
    Type *literalType(BuiltinKind k);
    Type *decayType(Type *t);
    Type *cloneTypeShallow(Type *t);
    // The C++ layer's types are unknown here, so copying one is its job.
    // A NEW node the caller owns, or 0.  The C layer has no type it cannot
    // already copy, so it never has one to offer.
    virtual Type *cloneForeignType(Type *) { return 0; }
    std::vector<Type*> ownedDecays;
    Type *commonType(BuiltinKind k);
    // Builtin types this pass forms, one per kind, owned here.
    std::map<int, Type*> builtinCache;
    IRReg lowerUnary(UnaryExpr *e);
    IRReg lowerAssign(BinaryExpr *e);
    // ++ / -- and += share one shape: take the address ONCE, load through it,
    // compute, store back.  Evaluating the target twice would be wrong the
    // moment it has a side effect.
    IRReg lowerIncDec(UnaryExpr *e);
    // The step for ++ on a pointer is the pointee's size, not one.
    IRReg stepFor(Type *t, int line);
    IRReg lowerShortCircuit(BinaryExpr *e);
    // Collapse any scalar to 0 or 1.  A logical operand is a truth value, not
    // the operand that happened to decide the answer.
    IRReg truth(IRReg value, Type *t, int line);
    virtual IRReg lowerCall(CallExpr *e, bool wantsResult);
    std::vector<IRReg> lowerArgs(CallExpr *e, Function *target, std::size_t skip);

    // --- hooks the C++ layer answers ----------------------------------
    // false when the node is not one this layer handles -- always, here.
    virtual bool lowerLayerValue(Expr *e, IRReg &out);
    virtual bool lowerLayerAddress(Expr *e, IRReg &out);

    // --- helpers ------------------------------------------------------
    int sizeOfType(Type *t) const;
    // Lowering needs sizes, not meanings, so this is a small local recompute
    // rather than a second type checker.
    virtual Type *typeOf(Expr *e);
    void pushScope();
    void popScope();
    int declareLocal(const std::string &name, int size, bool isParam, bool isFloat = false,
                     bool isObject = false);
    void emitGlobalInit(const std::vector<Decl*> &units);
    void emitGlobalFini(const std::vector<Decl*> &units);
    virtual void destroyGlobal(VarDecl *vd);
    // One global's initialiser, given its address.  Virtual because a global
    // object is CONSTRUCTED, and only the C++ layer knows that.
    virtual void initGlobal(VarDecl *vd, IRReg addr);
    // True when a value of this type is copied whole rather than loaded into a
    // register.  Only the C++ layer has such a type.
    virtual bool isObjectType(Type *t);
    // After a byte copy the destination must be made its own class again: the
    // copy carried the source's vptr, and the source may have been a derived
    // object sliced into a base.
    virtual void reassertVPtr(Type *t, IRReg addr, int line);
    // A function returning an object cannot hand it back in a register, and
    // its own frame is gone by the time the caller could copy it.  So the
    // CALLER supplies the space: a hidden pointer parameter, right after
    // `this`, that `return` copies into.  This is what makes  V c = a + b;
    // work at all.
    static const char *ReturnSlotName;
    bool returnsObject(Function *f);
    // The ADDRESS of an object-valued expression, whether it is a name or the
    // result of a call.
    IRReg lowerObjectValue(Expr *e);
    virtual bool yieldsObject(Expr *e) const;
    // An object passed BY VALUE: the callee gets a copy, and if the class
    // wrote a copy constructor that constructor is what makes it.
    virtual IRReg lowerByValueObject(Type *want, Expr *e, int line);
    // `return obj;` -- the same copy, into the slot the caller supplied.  Here
    // it is the bytes; the C++ layer overrides it to run the constructor.
    virtual void emitReturnObject(IRReg dest, Expr *e, int line);
    // A by-value argument that a copy constructor built lives in a temporary
    // of the caller's, and dies at the end of the expression that made it.
    // Nesting works because each call destroys only what it added.
    struct ArgTemp { int slot; Type *type; };
    std::vector<ArgTemp> argTemps;
    virtual void destroyArgTempsDownTo(std::size_t mark, int line);
    // Where an object-valued expression should be BUILT, when whoever asked
    // for it already has the space -- a variable being initialised from a
    // call, say.  Set it, lower the expression, and the outermost thing that
    // needed space takes it instead of declaring a temporary; anything nested
    // inside makes its own, as usual.  IR_NoReg means "make your own".
    // Taken at the TOP of whatever lowers a call, before its receiver and its
    // arguments -- those are nested expressions, and the destination belongs
    // to the outermost one.  Taking it later let  d = (a + b) * 2  give the
    // inner addition the slot meant for the multiplication.
    IRReg objectDest;
    IRReg takeObjectDest();
    // Space for a call's object result, and the address the callee fills in.
    // `given` is a destination already claimed by the caller, or IR_NoReg.
    IRReg allocReturnSlot(Function *target, int line, IRReg given = IR_NoReg);
    int findSlot(const std::string &name) const;
    virtual bool isReferenceExpr(Expr *e);  // its slot holds an address
    // A reference binds to an object, so it is passed and stored as that
    // object's ADDRESS.  C has no references, so this is false here.
    virtual bool isReferenceType(Type *t);
    // What a reference refers to.  A store through an int& is four bytes wide,
    // not eight -- the declared type decides, and T& is not the declared type
    // of the thing in memory.  Identity in the C layer, which has no T&.
    virtual Type *referentType(Type *t);
    std::map<std::string, Type*> localTypes;
    std::map<std::string, Type*> globalTypes;
    // Declared functions by name, bodiless ones included -- lowering needs
    // their parameter types to convert arguments at the call.
    std::map<std::string, Function*> functions;
    Type *currentReturnType;    // for converting a return expression

private:
    Lowering(const Lowering &);
    Lowering &operator=(const Lowering &);
};

} // namespace cc

#endif
