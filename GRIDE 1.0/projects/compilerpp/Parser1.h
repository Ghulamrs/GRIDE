// Parser1.h -- LAYER 2, the C++ layer's parser, namespace `cxx`.
//
// Derives from cc::Parser.  The expression chain, control flow and function
// bodies are inherited, not copied; this class adds classes, base clauses,
// constructors, initialiser lists, and the C++ forms of type, primary and
// postfix expression.
//
// C++98 only.

#ifndef PARSER1_H
#define PARSER1_H

#include "Token.h"
#include "AST1.h"
#include "Parser.h"

#include <set>
#include <string>
#include <vector>

namespace cxx {

class Parser : public cc::Parser {
public:
    Parser(const std::string &s, Diagnostics &d);

private:
    // new in the C++ layer
    ClassDecl *parseClass();
    // int Point::getX() { ... } at file scope: the body of a method declared
    // inside the class.  Returns 0 when the declaration is not qualified.
    Decl *parseOutOfLineDefinition();
    Decl *parseMemberDecl(const std::string &className, Access access);
    QualifiedName *parseQualifiedName();
    // Is the token stream sitting on  ClassName (  -- i.e. a constructor?
    // Needs two tokens of lookahead, which the inherited save()/restore()
    // rewind provides.
    bool looksLikeConstructor(const std::string &className);

    // Every class name seen so far.  Without it `(x)` reads as a cast to a
    // class named x, and plain C code like `return (x);` stops compiling.
    // A name is recorded as soon as it is parsed, so a class may mention
    // itself: `Node *next;`.
    std::set<std::string> classNames;
    bool namesAClass(const std::string &n) const;
    // The name of an operator member, read after the `operator` keyword.
    std::string operatorMemberName();
    // `friend` grants access; it declares no member.  An inline definition is
    // hoisted to file scope, which is where the function actually lives.
    void parseFriend();
    // V operator*(int k, V v) { ... } at file scope.  Only a non-member can
    // put the class on the RIGHT of the operator, which is the whole reason
    // this form exists.
    Decl *parseOperatorFunction();
    ClassDecl *classBeingParsed;        // set while a class body is parsed

    // extension points taken over from the C layer.
    // C++98 has no `override` keyword -- each signature must match
    // cc::Parser's exactly or this would silently hide instead of override.
    virtual cc::Decl *parseDeclaration();
    virtual cc::Expr *parsePrimary();
    virtual cc::Expr *parseMemberSuffix(cc::Expr *base);
    virtual cc::Type *parseType();
    virtual void parseFunctionTail(cc::Function *fn);
    virtual void parseVarInitializer(cc::VarDecl *vd);
};

} // namespace cxx

#endif
