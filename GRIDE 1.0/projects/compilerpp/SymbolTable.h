// SymbolTable.h -- scopes and the names in them.
//
// A stack of Scopes: the innermost is searched first and lookup walks outward.
// Class members get a scope of their own, pushed while a method body is
// analysed, which is what makes an unqualified `x` find the class's field.
//
// C++98 only.

#ifndef SYMBOLTABLE_H
#define SYMBOLTABLE_H

#include <cstddef>
#include <map>
#include <string>
#include <vector>

// The AST lives in two namespaces: the C layer is `cc`, the C++ layer is
// `cxx` (see the layering model at the top of AST.h).  Only the C layer's
// root and type nodes are needed here, so they are forward declared.
namespace cc {
    struct ASTNode;
    struct Type;
}

enum SymbolKind {
    SYM_Var,
    SYM_Type,
    SYM_Method,
    SYM_Field
};

struct Symbol {
    SymbolKind kind;
    std::string name;
    // Any AST node may own a symbol: cc::VarDecl, cc::Function, cxx::FieldDecl.
    // cc::ASTNode is the common root of all of them.
    cc::ASTNode *decl;
    cc::Type *type;         // not owned
    Symbol(SymbolKind k, const std::string &n, cc::ASTNode *d, cc::Type *t)
        : kind(k), name(n), decl(d), type(t) {}
};

class Scope {
public:
    Scope() {}
    ~Scope();
    // Returns false if the name is already declared in THIS scope.
    bool insert(const std::string &name, Symbol *sym);
    Symbol *lookup(const std::string &name) const;

private:
    std::map<std::string, Symbol*> table;

    Scope(const Scope &);
    Scope &operator=(const Scope &);
};

class SymbolTable {
public:
    SymbolTable();
    ~SymbolTable();

    void pushScope();
    void popScope();

    bool insert(const std::string &name, Symbol *sym);
    // Innermost outward.
    Symbol *lookup(const std::string &name) const;
    // Current scope only -- the redeclaration test.
    Symbol *lookupLocal(const std::string &name) const;

private:
    std::vector<Scope*> stack;

    SymbolTable(const SymbolTable &);
    SymbolTable &operator=(const SymbolTable &);
};

#endif
