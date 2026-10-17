// Semantic.h -- PASS 3, semantic analysis.
//
// Walks the one tree the layered parser produced, which mixes cc:: and cxx::
// nodes, so this pass is deliberately not split in two.
//
// Checks: names resolve and nothing is declared twice; reference initialisers
// and assignment targets are lvalues; types match on initialisation,
// assignment, arithmetic, calls and return; access control including protected
// through derivation; overrides, hiding and the virtual-destructor rule;
// initialiser-list membership, duplication and declaration order.
//
// C++98 only.

#ifndef SEMANTIC_H
#define SEMANTIC_H

#include <map>
#include <string>
#include <vector>

#include "AST.h"       // LAYER 1 nodes
#include "AST1.h"      // LAYER 2 nodes
#include "Diagnostics.h"
#include "SymbolTable.h"

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(Diagnostics &d);
    ~SemanticAnalyzer();

    // entry point: a whole translation unit
    void analyze(const std::vector<cc::Decl*> &units);

    // The resolved hierarchy, for the layout pass -- one answer to "what is
    // this class's base", with the cycles already broken.
    const std::map<std::string, cxx::ClassDecl*> &classMap() const { return classes; }

private:
    SymbolTable symbols;
    Diagnostics &diag;

    std::map<std::string, cxx::ClassDecl*> classes;
    // A name no longer identifies a function, so every declaration of a name
    // is kept and the call site chooses between them.
    std::map<std::string, std::vector<cc::Function*> > overloads;

    // Context for the function being analysed.
    cc::Type *currentReturnType;
    std::string currentClass;       // empty outside a method body
    // The function being analysed.  Access control needs it: a friend is
    // granted by name AND signature, so knowing the name is not enough.
    cc::Function *currentFunction;
    bool isFriendOf(cxx::ClassDecl *owner) const;
    // A ctor/dtor has no return type at all, which is not the same as void.
    bool currentIsCtorOrDtor;
    // Inside a const member function every field is const, because *this is.
    bool currentMethodIsConst;
    int loopDepth;                  // break/continue legality
    int switchDepth;                // `break` is legal inside a switch too

    // Types the analyzer forms itself belong to no AST node, so it owns them
    // and frees them in its destructor.  A formed type must own every node in
    // it -- borrowing a subtree the AST will delete leaves a dangling pointer.
    std::vector<cc::Type*> ownedTypes;
    cc::Type *makeBuiltin(cc::BuiltinKind k);
    cc::Type *cloneType(cc::Type *t);
    cc::Type *cloneTypeShape(cc::Type *t);
    cc::Type *makePointerTo(cc::Type *t);

    // passes
    void collectClasses(const std::vector<cc::Decl*> &units);
    // Runs before anything looks a member up: member lookup walks this chain.
    void resolveBases();
    // Attaches each out-of-line definition to the declaration inside its class.
    void attachOutOfLineDefinitions(const std::vector<cc::Decl*> &units);
    void resolveOverrides(cxx::ClassDecl *cd);
    void analyzeMemberInits(cxx::MethodDecl *ctor, cxx::ClassDecl *cd);
    void checkClassInvariants(cxx::ClassDecl *cd);
    // By argument count.  0 means the class declares no constructors, which is
    // legal and means there is nothing to call.
    cxx::MethodDecl *selectConstructor(cxx::ClassDecl *cd,
                                       const std::vector<cc::Expr*> &args,
                                       cc::ASTNode *at, const std::string &what);
    void recordScopeExitDestruction(cc::CompoundStmt *block,
                                    const std::vector<cc::VarDecl*> &declared);
    bool hasDestructor(cc::Type *t);
    // A bodyless declaration of a built-in name IS the binding to it, so it
    // has to agree with the machine's own signature.
    void checkNativeDeclaration(cc::Function *fn);
    // Names a parameter in a diagnostic, by name or, having none, by position.
    static std::string parameterText(cc::Function *fn, std::size_t i);
    bool needsDestructor(cxx::ClassDecl *cd);
    // A class that owns something destructible gets a destructor whether or
    // not one was written -- otherwise nothing runs its members' destructors.
    void synthesiseDestructors();
    // The copy constructor a class declares, or 0.
    // Exact enough to win outright, in overload resolution only.
    bool exactForOverload(cc::Type *arg, cc::Type *param);
    cxx::MethodDecl *copyConstructorOf(cxx::ClassDecl *cd);
    // A copy is memberwise: the base is copied by ITS copy constructor and so
    // is every member that has one.  A class that declares none of its own
    // still needs one when anything it is made of has one, or the copy is
    // done with memcpy and those constructors never run.
    bool needsCopyConstructor(cxx::ClassDecl *cd);
    // Whether one CAN be generated: every base and class-typed member the
    // generated list names must itself be constructible from one argument.
    bool canSynthesiseCopy(cxx::ClassDecl *cd, int depth = 0);
    // Generates it for cd, and first for every part cd's list will name.
    bool synthesiseCopyFor(cxx::ClassDecl *cd);
    void synthesiseCopyConstructors();
    void declareTopLevel(const std::vector<cc::Decl*> &units);
    void analyzeDecl(cc::Decl *d);
    void analyzeClass(cxx::ClassDecl *cd);
    void analyzeFunction(cc::Function *fn);
    void analyzeStmt(cc::Stmt *s);
    void analyzeSwitch(cc::SwitchStmt *s);
    void analyzeBlock(cc::CompoundStmt *block);
    void analyzeVarDecl(cc::VarDecl *vd, bool declareIt);
    // analyzeExpr is a thin wrapper that records the result on the node;
    // analyzeExprImpl is the analysis itself.
    cc::Type *analyzeExpr(cc::Expr *e, bool &isLValue);
    cc::Type *analyzeExprImpl(cc::Expr *e, bool &isLValue);

    // member lookup
    cxx::ClassDecl *findClass(const std::string &name);
    // Overloaded operators: the member an expression calls, and the check that
    // its one argument fits.
    // The function an operator expression calls: a member when the LEFT
    // operand is the object, otherwise a non-member -- which is the only form
    // that can put the class on the right, as in  3 * v.
    cc::Function *findOperator(cc::Expr *lhs, cc::Type *lt,
                               cc::Expr *rhs, cc::Type *rt,
                               cc::BinaryOp op, cc::ASTNode *at);
    cxx::MethodDecl *findCallOperator(cc::Type *ot, cc::CallExpr *call);
    cxx::MethodDecl *findIndexOperator(cxx::ClassDecl *cd, cc::Expr *index,
                                       cc::Type *it, bool objectConst, cc::ASTNode *at);
    cc::Function *findUnaryMinusOperator(cc::Expr *operand, cc::Type *t,
                                         cc::ASTNode *at);
    cxx::MethodDecl *findMemberOperator(cc::Type *lt, cc::BinaryOp op,
                                        cc::Expr *rhs, cc::Type *rt, cc::ASTNode *at);
    cc::Function *findFreeOperator(cc::Expr *lhs, cc::Type *lt,
                                   cc::Expr *rhs, cc::Type *rt,
                                   const std::string &name);
    bool isClassType(cc::Type *t);
    // Does this expression name something declared const?  A member of a const
    // object is const too, which is what stops  a.x = 1  through a const A&.
    bool isConstExpr(cc::Expr *e);
    static bool isNonConstReferenceTo(cc::Type *t);
    bool objectIsConst(cxx::MemberAccessExpr *ma);
    // One const check for every form of member call.
    void checkConstUse(cxx::MethodDecl *m, bool objectConst, cc::ASTNode *at);
    // Const may be added by a conversion, never removed.
    bool constQualificationOk(cc::Type *from, cc::Type *to);
    // Walks the base chain, most derived first -- which IS name hiding.
    // `foundIn` receives the class it was found in, for the diagnostic.
    cc::Decl *findMember(cxx::ClassDecl *cd, const std::string &member,
                         cxx::ClassDecl **foundIn = 0);
    static bool isDerivedFrom(cxx::ClassDecl *derived, cxx::ClassDecl *base);
    // public everywhere, protected inside the class or anything derived from
    // it, private only inside the class itself.
    bool memberIsAccessible(cc::Decl *m, cxx::ClassDecl *owner) const;
    cxx::ClassDecl *ownerClassOf(cc::Decl *m);
    static bool sameSignature(cc::Function *a, cc::Function *b);
    static cxx::Access memberAccess(cc::Decl *m);
    static cc::Type *memberType(cc::Decl *m);
    // Pushes a scope holding cd's members, so a method body sees them unqualified.
    void pushClassScope(cxx::ClassDecl *cd);

    // helpers
    void error(cc::ASTNode *at, const std::string &msg);
    static cc::Type *stripReference(cc::Type *t);
    // An array used in an expression becomes a pointer to its first element.
    // Only & and a declaration see the array type itself.
    cc::Type *decay(cc::Type *t);
    static std::string describe(cc::Type *t);
    static bool sameType(cc::Type *a, cc::Type *b);
    // Two questions that are not the same one: sameType asks whether two
    // VALUES have the same type (references stripped, const ignored), which is
    // what conversions need; sameDeclaredType asks whether two DECLARATIONS
    // are identical, which is what a signature is.  Conflating them let a
    // friend grant to peek(const A&) reach peek(A&).
    static bool sameDeclaredType(cc::Type *a, cc::Type *b);
    // sameType plus the upcasts single inheritance makes free.
    bool canConvert(cc::Type *from, cc::Type *to);
    // canConvert, plus the rule needing the EXPRESSION: literal 0 is the null
    // pointer constant, though its type is int.
    bool convertible(cc::Expr *fromExpr, cc::Type *from, cc::Type *to);
    static bool isNullPointerConstant(cc::Expr *e);
    cxx::ClassDecl *classOf(cc::Type *t);   // through one pointer or reference
    // The class a MEMBER is made of, through any number of array dimensions.
    // Not through a pointer: a pointer member is copied as the value it is.
    cxx::ClassDecl *memberClassOf(cc::Type *t);
    static bool isVoid(cc::Type *t);
    // The builtin kind a type names, or BK_Void when it names none.
    static bool builtinKindOf(cc::Type *t, cc::BuiltinKind &out);
    static bool isBoolType(cc::Type *t);
    bool isTestable(cc::Type *t);   // usable as a condition
    // The kind a type contributes to arithmetic.  bool answers BK_Int, because
    // that is what it promotes to -- which is why the C layer's kind table
    // needs no entry for a C++ type.
    static bool arithmeticKind(cc::Type *t, cc::BuiltinKind &out);
    cc::Type *makeBool();
    // Integral promotion: anything of rank below int becomes int.
    static cc::BuiltinKind promote(cc::BuiltinKind k);
    // The usual arithmetic conversions -- the common type two operands meet in.
    static cc::BuiltinKind usualArithmetic(cc::BuiltinKind a, cc::BuiltinKind b);
    // Would converting `from` to `to` lose information?
    static bool isNarrowing(cc::BuiltinKind from, cc::BuiltinKind to);
    // Legal conversions that may lose the value are allowed but reported.
    void warnIfNarrowing(cc::Expr *e, cc::Type *from, cc::Type *to,
                         cc::ASTNode *at, const std::string &what);
    // A literal whose value fits the target is not narrowing -- otherwise
    // `short s = 1;` would warn, and nothing small would ever be assignable.
    static bool literalFitsIn(cc::Expr *e, cc::BuiltinKind to);
    // A stream rather than sprintf: no fixed buffer, and MSVC stays quiet.
    static std::string countText(std::size_t n);
    // false when the type names a class that was never declared -- in which
    // case the caller skips further checks rather than cascading.
    bool checkTypeIsKnown(cc::Type *t, cc::ASTNode *at, const std::string &where);
    void checkCallArgs(cc::CallExpr *call, cc::Function *fn);
    // Picks the overload a call names.  An exact match wins outright; failing
    // that a single convertible candidate wins; anything else is an error the
    // programmer has to resolve.
    cc::Function *resolveOverload(const std::vector<cc::Function*> &candidates,
                                  cc::CallExpr *call, const std::string &name);
    // Do two declarations of one name describe the same function?
    static bool sameParams(cc::Function *a, cc::Function *b);
    // Every method of this name in the class and its bases, most derived
    // first -- the candidate set a member call chooses from.
    std::vector<cc::Function*> findMethods(cxx::ClassDecl *cd, const std::string &name);

    // not copyable (C++98 way: declared private, never defined)
    SemanticAnalyzer(const SemanticAnalyzer &);
    SemanticAnalyzer &operator=(const SemanticAnalyzer &);
};

#endif
