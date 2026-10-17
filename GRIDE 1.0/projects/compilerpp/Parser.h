// Parser.h -- LAYER 1, the C layer's parser, namespace `cc`.
//
// The base of the layered parser.  Every point where C++ extends C is virtual;
// see AST.h for the model and Parser1.h for what the C++ layer does with them.
//
// Errors are reported, never fatal: the parser tells Diagnostics and then
// resynchronises, so one mistake does not hide the next.
//
// C++98 only.

#ifndef PARSER_H
#define PARSER_H

#include "Token.h"
#include "AST.h"
#include "Lexer.h"
#include "Diagnostics.h"

#include <cstddef>
#include <string>
#include <vector>

namespace cc {

class Parser {
public:
    Parser(const std::string &s, Diagnostics &d);
    virtual ~Parser();               // virtual: this class is a base

    // The whole file: a list of declarations.  Virtual dispatch inside means a
    // cxx::Parser instance parses classes here too.
    std::vector<Decl*> parseTranslationUnit();

    // Convenience for the C layer on its own: parse a single function.
    Function *parseSingleFunction();

protected:
    void advance();
    Token cur;
    ::Lexer *lexer;
    Diagnostics &diag;

    // --- error reporting and recovery ------------------------------------
    void errorAtCurrent(const std::string &msg);
    bool expect(TokenKind k, const char *context);  // reports if it does not match
    bool match(TokenKind k);                        // consume if it matches
    void synchronize();         // panic-mode: skip to a plausible restart point
    // Reports the one message a reserved keyword deserves and skips the whole
    // construct, so an unsupported feature costs one diagnostic and not a
    // cascade of them.  Returns true when it consumed something.
    bool skipReservedConstruct();
    // Skips to the end of the current declaration or statement, counting
    // braces so a body is stepped over whole.
    void skipConstruct();
    // Steps over a balanced ( ... ), for a reserved word used as an operator.
    // A declaration made as a side effect of another: a friend function
    // defined inside a class body belongs at FILE scope, not in the class.
    std::vector<Decl*> pending;

    void skipParenGroup();
    bool peekIsStar();
    // `vector<int> v;` named as a template rather than read as a comparison.
    bool skipTemplateDeclaration();
    // Set by skipReservedConstruct so the caller does not resynchronise on top
    // of a skip that already landed cleanly.
    bool suppressSync;

    // --- speculation ------------------------------------------------------
    // Point p; and p.x = 1; both start with an identifier, so parseStatement()
    // tries the declaration rule and rewinds if it does not fit.
    struct State {
        Token cur;
        ::Lexer::Position lexPos;
    };
    State save() const;
    void restore(const State &st);

    // --- declarations -----------------------------------------------------
    // The return type and name are already consumed by the caller.
    Function *parseFunctionRest(Type *retType, const std::string &name);
    // Fills an already-created node, so the C++ layer can pass a MethodDecl --
    // which is a cc::Function -- and have this layer populate it.
    void parseFunctionParamsAndBody(Function *fn);
    // nameLine/nameCol point a diagnostic at the variable, not its type keyword.
    VarDecl *parseVarDeclTail(Type *type, const std::string &name,
                              int nameLine, int nameCol);

    // --- statements -------------------------------------------------------
    CompoundStmt *parseBlock();
    Stmt *parseIf();
    Stmt *parseWhile();
    Stmt *parseFor();
    Stmt *parseReturn();
    Stmt *parseDoWhile();
    Stmt *parseSwitch();
    Stmt *parseExprStatement();

    // --- the precedence chain, defined ONCE, here -------------------------
    Expr *parseExpression();
    Expr *parseAssign();
    Expr *parseLogicalOr();
    Expr *parseLogicalAnd();
    Expr *parseEquality();
    Expr *parseRelational();
    Expr *parseShift();
    Expr *parseAddSub();
    Expr *parseMulDiv();
    Expr *parseUnary();
    // (T)expr, told from a parenthesised expression by trying the type rule
    // and rewinding when it does not fit.
    Expr *parseCastOrParen();
    Expr *parsePostfix();
    Expr *parseCallSuffix(Expr *callee);
    // a[i] is desugared to *(a + i), which is what it means in C -- so it
    // needs no node, no type rule and no lowering of its own.
    Expr *parseIndexSuffix(Expr *base);

    // C's type grammar:  int   int*   int**
    Type *parsePointerSuffixes(Type *base);
    // In C the array part follows the NAME -- int a[10] -- so it is applied by
    // the declarator, not by parseType.  Dimensions nest inside out:
    // int a[3][4] is 3 arrays of 4 ints.
    Type *parseArraySuffixes(Type *element);

    // Recursive descent has exactly one failure mode a program can reach from
    // outside: nest deeply enough and the C++ stack runs out before the parse
    // does.  A limit turns a crash into a diagnostic.
    //
    // The number is not the parser's to choose.  What the parser accepts, the
    // semantic pass, the lowering and the AST's destructor each walk again,
    // recursively, with much larger frames -- analyzeExprImpl alone is 514
    // lines -- so the limit has to be one those passes survive on the smallest
    // stack this compiler is expected to run on.  Measured, on a chain of
    // `cout << x`, which is the most expensive link a program is likely to
    // write: 512KB dies between 60 and 80, 1MB between 120 and 160.  1MB is
    // the default main-thread stack on Windows and on iOS, so 100 is the
    // number, with the margin on the side of the constrained host.  256 was
    // chosen when the parser was the only thing counting, and a program at 256
    // crashed everything downstream on both.
    static const int MaxNesting = 100;
    int nesting;
    bool nestingReported;
    bool tooDeep();                 // reports once, then stays quiet

    // The same failure, reached the other way.  `a + b + c + ...` is parsed by
    // a LOOP, so the parser's own recursion never grows -- but the tree it
    // builds is one level deeper per operator, and every pass after the parser
    // walks that tree recursively.  So the parse survived 20,000 terms and the
    // semantic pass, the lowering and the AST's own destructor did not.
    //
    // Counted per outermost EXPRESSION, because the depth that matters is one
    // expression's, not a function's: four hundred short statements are fine
    // and a program made of them must stay fine.  `nesting` cannot answer that
    // question -- a block bumps it too, so inside any function it is never
    // zero and the count would run on across every statement in the body.
    int exprNesting;
    long chainLinks;
    bool chainReported;
    bool chainTooDeep();            // counts one link, reports once

    // --- extension points overridden by the C++ layer ---------------------
    virtual Decl *parseDeclaration();
    virtual Stmt *parseStatement();
    // The body of it.  parseStatement itself is only the depth count, so that
    // one place answers for every shape of nesting a statement can have.
    Stmt *parseStatementImpl();
    virtual Expr *parsePrimary();
    virtual Type *parseType();
    virtual Expr *parseMemberSuffix(Expr *base);    // 0 if not a member access
    virtual void parseFunctionTail(Function *fn);   // a ctor's initialiser list
    virtual void parseVarInitializer(VarDecl *vd);  // = expr, and C++'s (args)

private:
    // not copyable (C++98 way: declared private, never defined)
    Parser(const Parser &);
    Parser &operator=(const Parser &);
};

} // namespace cc

#endif
