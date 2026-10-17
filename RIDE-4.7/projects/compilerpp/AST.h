// AST.h -- LAYER 1, the C layer, namespace `cc`.
//
// ============================ LAYERING MODEL ============================
//
//   Two layers, and the second inherits the first:   cxx::X : public cc::X
//
//     cc::   AST.h  / AST.cpp  / Parser.h  / Parser.cpp    -- what C has
//     cxx::  AST1.h / AST1.cpp / Parser1.h / Parser1.cpp   -- what C++ adds
//
//   Layer 2 never duplicates layer 1.  The test for where a node belongs is
//   simply: does C already have this?
//
//   cc::ASTNode                                   (the single root)
//     |-- cc::Type
//     |     |-- cc::BuiltinType | cc::PointerType        int, T*
//     |     '-- cxx::ReferenceType | cxx::ClassType      T&, Point
//     |-- cc::Expr
//     |     |-- cc::NumberExpr | cc::IdentExpr
//     |     |-- cc::UnaryExpr | cc::BinaryExpr | cc::CallExpr
//     |     '-- cxx::MemberAccessExpr | cxx::ThisExpr
//     |         cxx::NewExpr | cxx::DeleteExpr
//     |-- cc::Stmt
//     |     |-- cc::CompoundStmt      { ... }, also the unit of scope
//     |     |-- cc::DeclStmt | cc::ExprStmt | cc::ReturnStmt
//     |     |-- cc::IfStmt | cc::WhileStmt | cc::ForStmt
//     |     '-- cc::BreakStmt | cc::ContinueStmt
//     '-- cc::Decl
//           |-- cc::VarDecl | cc::Function
//           |-- cxx::FieldDecl  : cc::Decl        adds access
//           |-- cxx::MethodDecl : cc::Function    adds access, virtual, ctor
//           '-- cxx::ClassDecl  : cc::Decl
//
//   Parser: cc::Parser holds the grammar; cxx::Parser derives from it and
//   answers the virtual hooks -- parseDeclaration, parseStatement, parseType,
//   parsePrimary, parseMemberSuffix, parseFunctionTail, parseVarInitializer.
//   The hooks run both ways: parsePostfix() asks parseMemberSuffix(), so
//   p.getX().y parses in one loop; parseStatement() asks parseType(), so a C
//   rule declares C++ types (Point p;) with no C++ statement code.
//
//   The result is ONE tree mixing both namespaces, which is why the semantic
//   pass is not split in two -- it walks the tree and uses dynamic_cast.
//
//   Passes: Parser -> Semantic (+ SymbolTable) -> Layout -> Lower (+ IR).
//
//   C++98 only, everywhere.  No `override` keyword exists, so a derived layer
//   re-declares a virtual with an exactly matching signature or silently hides.
//
// ========================================================================

#ifndef AST_H
#define AST_H

#include <cstddef>
#include <string>
#include <vector>
#include <iostream>

namespace cc {

struct ASTNode {
    int line;                   // copied off the token that started this node
    int col;

    ASTNode() : line(0), col(0) {}
    virtual ~ASTNode() {}
    virtual void print(int indent = 0) = 0;

protected:
    void printIndent(int n) {
        for (int i = 0; i < n; ++i) std::cout << "  ";
    }
};

// --- Types ------------------------------------------------------------

struct Type : public ASTNode {
    // `const` on a variable: it may not be assigned to, incremented, or
    // decremented after it is initialised.  It rides on the type because that
    // is where the parser puts it and where every check can reach it.
    bool isConst;
    Type() : isConst(false) {}
    virtual ~Type() {}
};

// The builtin types, as a kind rather than a name.  Every conversion rule in
// the language is stated in terms of three facts about a type -- is it integer
// or floating, is it signed, and what is its rank -- and a string carries none
// of them.  Sizes are this compiler's model, not the host's: `long` is 8 bytes
// here, as on Linux and macOS, even when targeting Windows.
//
// There is no `bool` here.  C89 has none, so it belongs to the C++ layer --
// see cxx::BoolType in AST1.h.  Nothing in this table needs an entry for it,
// because bool promotes to int the moment it enters arithmetic and never
// survives as the type of a computation.
enum BuiltinKind {
    BK_Void,
    BK_Char, BK_SChar, BK_UChar,
    BK_Short, BK_UShort,
    BK_Int, BK_UInt,
    BK_Long, BK_ULong,
    BK_Float, BK_Double
};

const char *builtinName(BuiltinKind k);
int  builtinSize(BuiltinKind k);
// Conversion rank: char < short < int < long < float < double.  Two types of
// equal rank differ only in signedness.
int  builtinRank(BuiltinKind k);
bool builtinIsInteger(BuiltinKind k);
bool builtinIsFloating(BuiltinKind k);
bool builtinIsSigned(BuiltinKind k);
bool builtinIsArithmetic(BuiltinKind k);   // anything but void

struct BuiltinType : public Type {
    BuiltinKind kind;
    BuiltinType(BuiltinKind k) : kind(k) {}
    const char *name() const { return builtinName(kind); }
    void print(int indent);
};

// T[n].  An array is NOT a pointer: it owns its elements and knows how many
// there are.  It only becomes a pointer when used in an expression, which the
// semantic pass calls decay.
struct ArrayType : public Type {
    Type *element;
    long count;
    ArrayType(Type *e, long n) : element(e), count(n) {}
    ~ArrayType() { delete element; }
    void print(int indent);
};

// Pointer type T*
struct PointerType : public Type {
    Type *base;
    PointerType(Type *b) : base(b) {}
    ~PointerType() { delete base; }
    void print(int indent);
};

// --- Expressions ------------------------------------------------------

// Enums rather than characters: == and && do not fit in a char.
enum BinaryOp {
    BIN_Add, BIN_Sub, BIN_Mul, BIN_Div, BIN_Mod,
    BIN_Assign,
    // a += b evaluates a ONCE, so it is its own operator rather than sugar for
    // a = a + b, which would evaluate a twice.
    BIN_AddAssign, BIN_SubAssign, BIN_MulAssign, BIN_DivAssign, BIN_ModAssign,
    BIN_EQ, BIN_NE, BIN_LT, BIN_GT, BIN_LE, BIN_GE,
    BIN_LAnd, BIN_LOr,
    BIN_Shl, BIN_Shr
};
const char *binaryOpText(BinaryOp op);
bool binaryOpIsComparison(BinaryOp op);
bool binaryOpIsLogical(BinaryOp op);
// True for = and for every compound form.
bool binaryOpIsAssignment(BinaryOp op);
// The arithmetic behind a compound assignment: += yields +.
BinaryOp binaryOpUnderlying(BinaryOp op);

enum UnaryOp {
    UN_Neg, UN_Not, UN_Deref, UN_AddrOf,
    // Prefix yields the new value, postfix the old one -- the only difference
    // between them, and the reason they are four operators and not two.
    UN_PreInc, UN_PreDec, UN_PostInc, UN_PostDec
};
bool unaryOpIsIncDec(UnaryOp op);
const char *unaryOpText(UnaryOp op);

struct Type;

// Every expression carries the type the semantic pass computed for it.
// Lowering used to work this out again from scratch, with a weaker algorithm,
// and the two disagreed -- which is how a float multiply came to be emitted as
// an integer one.  Semantic decides; lowering reads.  Owned by the analyzer.
struct Expr : public ASTNode {
    Type *resolvedType;
    Expr() : resolvedType(0) {}
};

// An integer or character literal.  Both are integer values; only their type
// differs, which is why one node carries them.
struct NumberExpr : public Expr {
    long value;
    BuiltinKind kind;
    NumberExpr(long v, BuiltinKind k = BK_Int) : value(v), kind(k) {}
    void print(int indent);
};

struct FloatExpr : public Expr {
    double value;
    BuiltinKind kind;           // BK_Float or BK_Double
    FloatExpr(double v, BuiltinKind k) : value(v), kind(k) {}
    void print(int indent);
};

// "text" -- lowered to a pointer into the module's string data.
struct StringExpr : public Expr {
    std::string value;
    StringExpr(const std::string &v) : value(v) {}
    void print(int indent);
};

struct IdentExpr : public Expr {
    std::string name;
    IdentExpr(const std::string &n) : name(n) {}
    void print(int indent);
};

struct Function;                    // an overloaded operator resolves to one

struct UnaryExpr : public Expr {
    UnaryOp op;
    Expr *operand;
    Function *resolvedOperator;         // an overloaded unary '-'; not owned
    UnaryExpr(UnaryOp o, Expr *e) : op(o), operand(e), resolvedOperator(0) {}
    ~UnaryExpr() { delete operand; }
    void print(int indent);
};

// a[i].  Kept as a node rather than desugared to *(a+i) in the parser,
// because a class may overload it and the parser does not know types.  For a
// pointer or an array it lowers to exactly that sum, so nothing else changes.
struct IndexExpr : public Expr {
    Expr *base;
    Expr *index;
    Function *resolvedOperator;     // a class's operator[]; 0 otherwise
    IndexExpr(Expr *b, Expr *i) : base(b), index(i), resolvedOperator(0) {}
    ~IndexExpr();
    void print(int indent);
};

struct BinaryExpr : public Expr {
    BinaryOp op;
    Expr *lhs;
    Expr *rhs;
    // An overloaded operator, chosen by the semantic pass exactly as a call's
    // overload is.  When set, this expression IS a call; not owned.
    Function *resolvedOperator;
    BinaryExpr(BinaryOp o, Expr *l, Expr *r)
        : op(o), lhs(l), rhs(r), resolvedOperator(0) {}
    ~BinaryExpr();
    void print(int indent);
};

// (T)expr
struct CastExpr : public Expr {
    Type *type;
    Expr *expr;
    CastExpr(Type *t, Expr *e) : type(t), expr(e) {}
    ~CastExpr();
    void print(int indent);
};


struct CallExpr : public Expr {
    Expr *callee;
    std::vector<Expr*> args;
    // Which function this call resolved to, chosen by the semantic pass once
    // overloading made the name alone insufficient.  Lowering uses it rather
    // than resolving again; not owned.
    Function *resolved;
    CallExpr(Expr *c) : callee(c), resolved(0) {}
    ~CallExpr();
    void print(int indent);
};

// --- Declarations -----------------------------------------------------

struct Decl : public ASTNode {
    virtual ~Decl() {}
};

// One node for a variable wherever it appears: at file scope, or inside a
// block wrapped in a DeclStmt.
struct VarDecl : public Decl {
    Type *type;
    std::string name;
    Expr *init;                 // for  = expr ; 0 otherwise
    // Direct initialisation, Point q(1, 2) -- an alternative to init, never both.
    std::vector<Expr*> ctorArgs;
    bool hasCtorArgs;           // true even for  Point q();
    // Which constructor those arguments selected.  Chosen by the semantic
    // pass, exactly as a call's overload is; not owned.
    Function *resolvedCtor;
    VarDecl(Type *t, const std::string &n, Expr *i)
        : type(t), name(n), init(i), hasCtorArgs(false), resolvedCtor(0) {}
    ~VarDecl();
    void print(int indent);
};

struct Stmt;
struct CompoundStmt;

struct Function : public Decl {
    Type *retType;
    std::string name;
    std::vector<VarDecl*> params;
    CompoundStmt *body;         // 0 when this is only a declaration
    Function(Type *r, const std::string &n) : retType(r), name(n), body(0) {}
    ~Function();
    void print(int indent);
    virtual void printSignature(int indent);
    virtual void printBodyPrefix(int indent);   // a ctor's initialiser list
};

// --- Statements -------------------------------------------------------
struct Stmt : public ASTNode {};

// Also the unit of scope, which is why it is a node.
struct CompoundStmt : public Stmt {
    std::vector<Stmt*> body;
    // Class-typed locals in REVERSE declaration order: the destructors the
    // lowering phase runs on every path out of this block.  Filled in by the
    // semantic pass; aliases into the block's own DeclStmts, not owned.
    std::vector<VarDecl*> destroyAtExit;
    ~CompoundStmt();
    void print(int indent);
};

struct DeclStmt : public Stmt {
    VarDecl *var;
    DeclStmt(VarDecl *v) : var(v) {}
    ~DeclStmt() { delete var; }
    void print(int indent);
};

struct ExprStmt : public Stmt {
    Expr *expr;
    ExprStmt(Expr *e) : expr(e) {}
    ~ExprStmt() { delete expr; }
    void print(int indent);
};

struct ReturnStmt : public Stmt {
    Expr *expr;                 // 0 for a bare  return;
    ReturnStmt(Expr *e) : expr(e) {}
    ~ReturnStmt() { delete expr; }
    void print(int indent);
};

struct IfStmt : public Stmt {
    Expr *cond;
    Stmt *thenBranch;
    Stmt *elseBranch;           // may be 0
    IfStmt(Expr *c, Stmt *t, Stmt *e) : cond(c), thenBranch(t), elseBranch(e) {}
    ~IfStmt();
    void print(int indent);
};

struct WhileStmt : public Stmt {
    Expr *cond;
    Stmt *body;
    WhileStmt(Expr *c, Stmt *b) : cond(c), body(b) {}
    ~WhileStmt();
    void print(int indent);
};

// do body while (cond);  -- the body runs before the condition is first tested.
struct DoWhileStmt : public Stmt {
    Stmt *body;
    Expr *cond;
    DoWhileStmt(Stmt *b, Expr *c) : body(b), cond(c) {}
    ~DoWhileStmt();
    void print(int indent);
};

// A case label is a LABEL, not a block: control enters at the matching one and
// runs on through the rest until a break.  Modelling it as a statement inside
// the switch body is what makes fall-through work by construction.
struct CaseStmt : public Stmt {
    long value;
    bool isDefault;
    CaseStmt(long v, bool d) : value(v), isDefault(d) {}
    void print(int indent);
};

struct SwitchStmt : public Stmt {
    Expr *cond;
    CompoundStmt *body;
    SwitchStmt(Expr *c, CompoundStmt *b) : cond(c), body(b) {}
    ~SwitchStmt();
    void print(int indent);
};

// any of init, cond and step may be 0
struct ForStmt : public Stmt {
    Stmt *init;
    Expr *cond;
    Expr *step;
    Stmt *body;
    ForStmt(Stmt *i, Expr *c, Expr *s, Stmt *b) : init(i), cond(c), step(s), body(b) {}
    ~ForStmt();
    void print(int indent);
};

struct BreakStmt : public Stmt {
    void print(int indent);
};

struct ContinueStmt : public Stmt {
    void print(int indent);
};

} // namespace cc

#endif
