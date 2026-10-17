// Parser.cpp
//
// C++98 only.

#include "Parser.h"

#include <cstdlib>
#include <string>

namespace cc {

Parser::Parser(const std::string &s, Diagnostics &d)
    : lexer(createLexer(s)), diag(d), suppressSync(false), nesting(0), nestingReported(false),
      exprNesting(0), chainLinks(0), chainReported(false) {
    advance();
}

Parser::~Parser() {
    delete lexer;
}

void Parser::advance() {
    cur = lexer->nextToken();
}

// --- error reporting and recovery -------------------------------------

void Parser::errorAtCurrent(const std::string &msg) {
    diag.error(cur.line, cur.col, msg);
}

bool Parser::expect(TokenKind k, const char *context) {
    if (cur.kind == k) { advance(); return true; }
    errorAtCurrent(std::string("expected ") + tokenName(k) + " " + context
                   + ", found " + tokenName(cur.kind));
    return false;
}

bool Parser::match(TokenKind k) {
    if (cur.kind != k) return false;
    advance();
    return true;
}

// One message, then step over the whole construct.  A feature the subset does
// not have should cost the reader one line, not twenty.
bool Parser::skipReservedConstruct() {
    if (cur.kind != TOK_RESERVED) return false;

    // `using namespace std;` is stepped over WITHOUT a message, for the same
    // reason `std::` is accepted in an expression: this language's <iostream>
    // already puts cout, cin and endl at global scope, so the directive asks
    // for what is true and the program means the same with it or without it.
    // Every other `using` is still refused -- this is the one whose only
    // effect is the effect the language already has.
    if (cur.text == "using") {
        const State probe = save();
        advance();
        if (cur.kind == TOK_RESERVED && cur.text == "namespace") {
            advance();
            if (cur.kind == TOK_IDENTIFIER && cur.text == "std") {
                advance();
                if (cur.kind == TOK_SEMI) {
                    advance();
                    // Stepped over cleanly, so the caller must NOT resynchronise.
                    // Without this the directive returned a null declaration that
                    // read as a failed parse, and parseTranslationUnit's
                    // `if (!d) synchronize()` then skipped the token after it: a
                    // `template` on the next line was swallowed, and what was left
                    // -- `<class T>` -- was parsed as a class, so the one thing the
                    // reader needed to be told, that templates are excluded, was the
                    // one thing four cascading errors never said.
                    suppressSync = true;
                    return true;
                }
            }
        }
        restore(probe);
    }

    const char *help = reservedWordHelp(cur.text);
    errorAtCurrent(help ? help : "this keyword is not supported");
    skipConstruct();
    suppressSync = true;
    return true;
}

// `vector<int> v;` -- a template instantiation, which this version does not
// have and which nothing recognised.  The '<' was read as a comparison, so the
// type never existed and what came out was five errors about expressions, not
// one about templates, in the shape a beginner is most likely to type.
//
// It has to be a probe, because `a < b;` is an ordinary comparison and looks
// identical up to the '<'.  What separates them is what comes AFTER the
// matching '>': a name being declared.  That is the same test the class-name
// path already makes for `Widget w;`.
bool Parser::skipTemplateDeclaration() {
    if (cur.kind != TOK_IDENTIFIER) return false;
    const State probe = save();

    // Past any qualifier first: the name a user writes is `std::vector<int>`
    // far more often than `vector<int>`, and the qualifier is handled in the
    // expression parser, which this never reaches.
    while (cur.kind == TOK_IDENTIFIER) {
        advance();
        if (cur.kind != TOK_COLONCOLON) break;
        advance();
    }
    if (cur.kind != TOK_LT) { restore(probe); return false; }

    int depth = 0;
    while (cur.kind != TOK_EOF && cur.kind != TOK_SEMI &&
           cur.kind != TOK_LBRACE && cur.kind != TOK_RBRACE) {
        if (cur.kind == TOK_LT) ++depth;
        else if (cur.kind == TOK_GT) {
            --depth;
            advance();
            if (depth == 0) break;
            continue;
        }
        advance();
    }
    // A declaration names something; a comparison does not.
    const bool declaring = (depth == 0 && cur.kind == TOK_IDENTIFIER);
    restore(probe);
    if (!declaring) return false;

    errorAtCurrent("templates are not supported");
    skipConstruct();
    suppressSync = true;
    return true;
}

// One token past a '(' -- enough to tell a function pointer's declarator from
// an ordinary parenthesised expression.
bool Parser::peekIsStar() {
    const State st = save();
    advance();                          // consume '('
    const bool star = (cur.kind == TOK_STAR);
    restore(st);
    return star;
}

void Parser::skipParenGroup() {
    if (cur.kind != TOK_LPAREN) return;
    int depth = 0;
    while (cur.kind != TOK_EOF) {
        if (cur.kind == TOK_LPAREN) ++depth;
        else if (cur.kind == TOK_RPAREN) {
            --depth;
            advance();
            if (depth == 0) return;
            continue;
        }
        advance();
    }
}

void Parser::skipConstruct() {
    // try { ... } catch ( ... ) { ... }  is ONE construct, however many
    // keywords it spells, so it earns one message.
    const bool isTry = (cur.kind == TOK_RESERVED && cur.text == "try");
    int depth = 0;
    while (cur.kind != TOK_EOF) {
        if (cur.kind == TOK_LBRACE) { ++depth; advance(); continue; }
        if (cur.kind == TOK_RBRACE) {
            if (depth == 0) return;         // belongs to an enclosing block
            --depth;
            advance();
            if (depth == 0) {
                // try { ... } catch ( ... ) { ... } is one construct: keep
                // going rather than leaving `catch` to earn a second message.
                if (isTry && cur.kind == TOK_RESERVED && cur.text == "catch") {
                    advance();
                    skipParenGroup();
                    continue;
                }
                match(TOK_SEMI);            // a class or enum body may end with ;
                return;
            }
            continue;
        }
        if (cur.kind == TOK_SEMI && depth == 0) { advance(); return; }
        advance();
    }
}

// Skip to a token that plausibly begins something new.
void Parser::synchronize() {
    while (cur.kind != TOK_EOF) {
        if (cur.kind == TOK_SEMI) { advance(); return; }
        switch (cur.kind) {
        case TOK_RBRACE:
        case TOK_CLASS:
        case TOK_STRUCT:
        case TOK_IF:
        case TOK_WHILE:
        case TOK_DO:
        case TOK_SWITCH:
        case TOK_FOR:
        case TOK_RETURN:
        case TOK_INT:
        case TOK_CHAR:
        case TOK_VOID:
        case TOK_SHORT:
        case TOK_LONG:
        case TOK_SIGNED:
        case TOK_UNSIGNED:
        case TOK_FLOAT:
        case TOK_DOUBLE:
            return;
        default:
            advance();
        }
    }
}

// --- speculation ------------------------------------------------------

Parser::State Parser::save() const {
    State st;
    st.cur = cur;
    st.lexPos = lexer->tell();
    return st;
}

void Parser::restore(const State &st) {
    cur = st.cur;
    lexer->seek(st.lexPos);
}

// --- translation unit -------------------------------------------------

std::vector<Decl*> Parser::parseTranslationUnit() {
    std::vector<Decl*> units;
    while (cur.kind != TOK_EOF) {
        const std::size_t posBefore = lexer->tell().offset;
        suppressSync = false;
        Decl *d = parseDeclaration();       // virtual
        if (d) units.push_back(d);
        // Anything hoisted out of that declaration follows it.
        for (std::size_t k = 0; k < pending.size(); ++k) units.push_back(pending[k]);
        pending.clear();
        if (suppressSync) { suppressSync = false; continue; }
        // Only a FAILED parse resynchronises: one that produced a node left the
        // parser somewhere sensible even if it also reported.  The progress
        // check is the backstop that makes this loop terminate regardless.
        if (!d) synchronize();
        if (lexer->tell().offset == posBefore && cur.kind != TOK_EOF) advance();
    }
    return units;
}

Function *Parser::parseSingleFunction() {
    Type *t = parseType();
    if (!t) { errorAtCurrent("expected a return type"); return 0; }
    if (cur.kind != TOK_IDENTIFIER) {
        errorAtCurrent("expected a function name");
        delete t;
        return 0;
    }
    const std::string name = cur.text;
    advance();
    return parseFunctionRest(t, name);
}

// --- declarations -----------------------------------------------------

Decl *Parser::parseDeclaration() {
    if (skipTemplateDeclaration()) return 0;    // `vector<int> v;` at file scope
    if (skipReservedConstruct()) return 0;
    // #include is accepted and ignored.  There is no header to read -- a
    // native binds by being declared without a body -- but every C++ program a
    // student has ever seen opens with one, and stopping there helps nobody.
    // Any OTHER directive is still refused, by name.
    if (cur.kind == TOK_HASH) {
        const int line = cur.line;
        advance();
        const bool isInclude = (cur.kind == TOK_IDENTIFIER && cur.text == "include");
        if (!isInclude) {
            const std::string what = (cur.kind == TOK_IDENTIFIER)
                                   ? ("'#" + cur.text + "'")
                                   : std::string("this directive");
            errorAtCurrent(what + " is not supported");
        }
        while (cur.kind != TOK_EOF && cur.line == line) advance();
        suppressSync = true;
        return 0;
    }
    Type *t = parseType();              // virtual
    if (!t) {
        errorAtCurrent(std::string("expected a declaration, found ") + tokenName(cur.kind));
        advance();                      // guarantee progress
        return 0;
    }
    // int (*p)(int) -- a type, then a parenthesised '*'.  Nothing else in this
    // grammar looks like that, so it can be named instead of misread.
    if (cur.kind == TOK_LPAREN && peekIsStar()) {
        errorAtCurrent("function pointers are not supported");
        delete t;
        skipConstruct();
        return 0;
    }
    if (cur.kind != TOK_IDENTIFIER) {
        errorAtCurrent("expected a name in declaration");
        delete t;
        return 0;
    }
    const int line = cur.line, col = cur.col;
    const std::string name = cur.text;
    advance();

    Decl *d = 0;
    if (cur.kind == TOK_LPAREN) d = parseFunctionRest(t, name);
    else                        d = parseVarDeclTail(t, name, line, col);
    if (d) { d->line = line; d->col = col; }
    return d;
}

// '(' [ type IDENT { ',' type IDENT } ] ')' then ';' or a body.
Function *Parser::parseFunctionRest(Type *retType, const std::string &name) {
    Function *fn = new Function(retType, name);
    parseFunctionParamsAndBody(fn);
    return fn;
}

void Parser::parseFunctionParamsAndBody(Function *fn) {
    if (!expect(TOK_LPAREN, "in function declaration")) return;

    while (cur.kind != TOK_RPAREN && cur.kind != TOK_EOF) {
        if (cur.kind == TOK_ELLIPSIS) {
            errorAtCurrent("variadic functions are not supported");
            while (cur.kind != TOK_RPAREN && cur.kind != TOK_EOF) advance();
            break;
        }
        Type *pt = parseType();                     // virtual
        if (!pt) { errorAtCurrent("expected a parameter type"); break; }
        std::string pname;
        int pline = cur.line, pcol = cur.col;
        if (cur.kind == TOK_IDENTIFIER) { pname = cur.text; advance(); }
        // Each is rejected by name: a bare "expected ')'" says nothing about
        // which feature the program was reaching for.
        if (cur.kind == TOK_ASSIGN) {
            errorAtCurrent("default arguments are not supported");
            while (cur.kind != TOK_COMMA && cur.kind != TOK_RPAREN &&
                   cur.kind != TOK_EOF) advance();
        }
        if (cur.kind == TOK_LBRACKET) {
            errorAtCurrent("array parameters are not supported; "
                           "pass a pointer");
            while (cur.kind != TOK_COMMA && cur.kind != TOK_RPAREN &&
                   cur.kind != TOK_EOF) advance();
        }
        VarDecl *param = new VarDecl(pt, pname, 0);
        param->line = pline;
        param->col = pcol;
        fn->params.push_back(param);
        if (!match(TOK_COMMA)) break;
    }
    expect(TOK_RPAREN, "after parameter list");

    // A trailing const is a C++ member-function thing, so the hook takes it.
    parseFunctionTail(fn);                          // virtual: C++ adds  : x(1)

    if (match(TOK_SEMI)) return;                    // a declaration only
    if (cur.kind == TOK_LBRACE) {
        fn->body = parseBlock();
        return;
    }
    // `= 0` is the one thing that plausibly follows a signature and is not a
    // body, so it is worth its own sentence rather than a punctuation complaint.
    if (cur.kind == TOK_ASSIGN) {
        errorAtCurrent("pure virtual functions are not supported; "
                       "give the method a body");
        while (cur.kind != TOK_SEMI && cur.kind != TOK_EOF) advance();
        match(TOK_SEMI);
        return;
    }
    errorAtCurrent("expected ';' or '{' after function " + fn->name);
}

// IDENT already consumed:  [ '=' expr ] ';'
VarDecl *Parser::parseVarDeclTail(Type *type, const std::string &name,
                                  int nameLine, int nameCol) {
    type = parseArraySuffixes(type);            // int a[10]
    VarDecl *vd = new VarDecl(type, name, 0);
    vd->line = nameLine;
    vd->col = nameCol;
    parseVarInitializer(vd);            // virtual: C++ adds  (args)
    // `int a = 1, b = 2;` -- one type, several names.  The comma is where it
    // shows, so the comma is where it is named; without this the reader was
    // told a semicolon was expected, which is true and unhelpful.
    if (cur.kind == TOK_COMMA) {
        errorAtCurrent("declaring more than one variable in a statement is not "
                       "supported; write a declaration each");
        while (cur.kind != TOK_SEMI && cur.kind != TOK_EOF) advance();
    }
    expect(TOK_SEMI, ("after declaration of " + name).c_str());
    return vd;
}

Type *Parser::parseArraySuffixes(Type *element) {
    std::vector<long> dims;
    // Where the first '[' was.  A type has a position for the same reason an
    // expression does -- something later has to be able to point at it, and
    // Layout does when the array turns out not to fit in the machine.
    const int line = cur.line;
    const int col = cur.col;
    while (cur.kind == TOK_LBRACKET) {
        advance();
        long n = 0;
        if (cur.kind == TOK_NUMBER) {
            n = cur.numberValue;
            advance();
        } else {
            errorAtCurrent("an array bound must be a constant integer");
        }
        if (n <= 0) errorAtCurrent("an array must have at least one element");
        dims.push_back(n > 0 ? n : 1);
        expect(TOK_RBRACKET, "after an array bound");
    }
    // Built inside out, so the LAST bound is the innermost element count.
    for (std::size_t i = dims.size(); i > 0; --i) {
        ArrayType *at = new ArrayType(element, dims[i - 1]);
        at->line = line;
        at->col = col;
        element = at;
    }
    return element;
}

void Parser::parseVarInitializer(VarDecl *vd) {
    if (!match(TOK_ASSIGN)) return;
    // int a[3] = {1, 2, 3};  -- a brace list, which this version does not take.
    // Name it, because "expected an expression, found '{'" explains nothing.
    if (cur.kind == TOK_LBRACE) {
        errorAtCurrent("brace initialisers are not supported");
        int depth = 0;
        while (cur.kind != TOK_EOF) {
            if (cur.kind == TOK_LBRACE) ++depth;
            else if (cur.kind == TOK_RBRACE) { --depth; advance(); if (!depth) break; continue; }
            advance();
        }
        return;
    }
    vd->init = parseExpression();
}

// --- statements -------------------------------------------------------

CompoundStmt *Parser::parseBlock() {
    CompoundStmt *block = new CompoundStmt();
    block->line = cur.line;
    block->col = cur.col;
    if (tooDeep()) return block;
    if (!expect(TOK_LBRACE, "to open a block")) return block;
    ++nesting;
    while (cur.kind != TOK_RBRACE && cur.kind != TOK_EOF) {
        const std::size_t posBefore = lexer->tell().offset;
        suppressSync = false;
        Stmt *s = parseStatement();         // virtual
        if (s) block->body.push_back(s);
        if (suppressSync) { suppressSync = false; continue; }
        if (!s) synchronize();
        if (lexer->tell().offset == posBefore && cur.kind != TOK_EOF) advance();
    }
    expect(TOK_RBRACE, "to close a block");
    --nesting;
    return block;
}

// Declaration and expression share a first token -- Point p; vs p.x = 1; -- so
// the declaration rule is tried first and rewound if it fails.  parseType() is
// VIRTUAL, so this one C rule also declares the C++ layer's types.
Stmt *Parser::parseStatement() {
    // Statements nest too, and not only through blocks -- `else if` chains an
    // if-statement onto an if-statement with no block between them, so the
    // depth grows without parseCompound ever seeing it.  Twenty thousand of
    // them parsed, and then the semantic pass walked the same shape and did
    // not.  This is the same counter parseCompound uses, and every nested
    // statement is a level of it however it was reached.
    if (tooDeep()) return 0;
    ++nesting;
    Stmt *s = parseStatementImpl();
    --nesting;
    return s;
}

Stmt *Parser::parseStatementImpl() {
    if (skipReservedConstruct()) return 0;

    // A label is only useful with goto, which this subset does not have, so it
    // is reported here rather than left to fail as a stray expression.
    if (cur.kind == TOK_IDENTIFIER) {
        const State st = save();
        const std::string name = cur.text;
        advance();
        if (cur.kind == TOK_COLON) {
            errorAtCurrent("labels are not supported");
            advance();
            suppressSync = true;
            return 0;
        }
        restore(st);
        (void)name;
    }

    const int line = cur.line, col = cur.col;
    Stmt *s = 0;

    switch (cur.kind) {
    case TOK_LBRACE:   s = parseBlock(); break;
    case TOK_IF:       s = parseIf(); break;
    case TOK_WHILE:    s = parseWhile(); break;
    case TOK_DO:       s = parseDoWhile(); break;
    case TOK_SWITCH:   s = parseSwitch(); break;
    case TOK_CASE: {
        advance();
        Expr *v = parseExpression();
        // A label is a constant, and -1 is as constant as 1.
        long value = 0;
        bool constant = false;
        if (NumberExpr *n = dynamic_cast<NumberExpr*>(v)) {
            value = n->value;
            constant = true;
        } else if (UnaryExpr *u = dynamic_cast<UnaryExpr*>(v)) {
            NumberExpr *inner = dynamic_cast<NumberExpr*>(u->operand);
            if (inner && u->op == UN_Neg) {
                value = -inner->value;
                constant = true;
            }
        }
        if (!constant) errorAtCurrent("a case label needs a constant integer");
        s = new CaseStmt(value, false);
        delete v;
        expect(TOK_COLON, "after a case label");
        break;
    }
    case TOK_DEFAULT:
        advance();
        expect(TOK_COLON, "after 'default'");
        s = new CaseStmt(0, true);
        break;
    case TOK_FOR:      s = parseFor(); break;
    case TOK_RETURN:   s = parseReturn(); break;
    case TOK_BREAK:    advance(); expect(TOK_SEMI, "after break"); s = new BreakStmt(); break;
    case TOK_CONTINUE: advance(); expect(TOK_SEMI, "after continue"); s = new ContinueStmt(); break;
    case TOK_SEMI:     advance(); return 0;         // an empty statement
    default: {
        if (skipTemplateDeclaration()) return 0;
        State st = save();
        Type *t = parseType();                      // virtual
        if (t) {
            if (cur.kind == TOK_IDENTIFIER) {
                const std::string name = cur.text;
                const int nameLine = cur.line, nameCol = cur.col;
                advance();
                s = new DeclStmt(parseVarDeclTail(t, name, nameLine, nameCol));
                break;
            }
            // int (*p)(int); -- a type, then a parenthesised '*'.
            if (cur.kind == TOK_LPAREN && peekIsStar()) {
                errorAtCurrent("function pointers are not supported");
                delete t;
                skipConstruct();
                suppressSync = true;
                return 0;
            }
            delete t;                               // a type, but not a declaration
        }
        restore(st);
        s = parseExprStatement();
        break;
    }
    }

    if (s) { s->line = line; s->col = col; }
    return s;
}

Stmt *Parser::parseIf() {
    advance();                                      // consume 'if'
    expect(TOK_LPAREN, "after 'if'");
    Expr *cond = parseExpression();
    expect(TOK_RPAREN, "after if condition");
    Stmt *thenBranch = parseStatement();
    Stmt *elseBranch = 0;
    if (match(TOK_ELSE)) elseBranch = parseStatement();
    return new IfStmt(cond, thenBranch, elseBranch);
}

Stmt *Parser::parseWhile() {
    advance();                                      // consume 'while'
    expect(TOK_LPAREN, "after 'while'");
    Expr *cond = parseExpression();
    expect(TOK_RPAREN, "after while condition");
    return new WhileStmt(cond, parseStatement());
}

Stmt *Parser::parseFor() {
    advance();                                      // consume 'for'
    expect(TOK_LPAREN, "after 'for'");
    Stmt *init = 0;
    if (cur.kind != TOK_SEMI) init = parseStatement();   // consumes its own ';'
    else advance();
    Expr *cond = 0;
    if (cur.kind != TOK_SEMI) cond = parseExpression();
    expect(TOK_SEMI, "after for condition");
    Expr *step = 0;
    if (cur.kind != TOK_RPAREN) step = parseExpression();
    expect(TOK_RPAREN, "after for clauses");
    Stmt *body = parseStatement();

    // `for (T t; ...)` is `{ T t; for (; ...) ... }`.  Rewriting it here gives
    // the init declaration an ordinary block to live in, so it is destroyed on
    // every path out -- the loop ending, a break, or a return -- without any
    // rule in lowering having to know about for-init.
    if (dynamic_cast<DeclStmt*>(init)) {
        CompoundStmt *wrap = new CompoundStmt();
        wrap->line = init->line;
        wrap->col = init->col;
        wrap->body.push_back(init);
        wrap->body.push_back(new ForStmt(0, cond, step, body));
        return wrap;
    }
    return new ForStmt(init, cond, step, body);
}

Stmt *Parser::parseDoWhile() {
    advance();                                      // consume 'do'
    Stmt *body = parseStatement();
    expect(TOK_WHILE, "after a do body");
    expect(TOK_LPAREN, "after 'while'");
    Expr *cond = parseExpression();
    expect(TOK_RPAREN, "after a do-while condition");
    expect(TOK_SEMI, "after do-while");
    return new DoWhileStmt(body, cond);
}

Stmt *Parser::parseSwitch() {
    advance();                                      // consume 'switch'
    expect(TOK_LPAREN, "after 'switch'");
    Expr *cond = parseExpression();
    expect(TOK_RPAREN, "after a switch subject");
    if (cur.kind != TOK_LBRACE) {
        errorAtCurrent("expected '{' after switch");
        return new SwitchStmt(cond, new CompoundStmt());
    }
    return new SwitchStmt(cond, parseBlock());
}

Stmt *Parser::parseReturn() {
    advance();                                      // consume 'return'
    Expr *e = 0;
    if (cur.kind != TOK_SEMI) e = parseExpression();
    expect(TOK_SEMI, "after return");
    return new ReturnStmt(e);
}

Stmt *Parser::parseExprStatement() {
    Expr *e = parseExpression();
    if (!e) { advance(); return 0; }                // guarantee progress
    expect(TOK_SEMI, "after expression statement");
    return new ExprStmt(e);
}

// --- the type grammar -------------------------------------------------

Type *Parser::parsePointerSuffixes(Type *base) {
    while (cur.kind == TOK_STAR) {
        advance();
        base = new PointerType(base);
        // `char* const p` -- the const after the star belongs to the POINTER,
        // which may then not be moved.  `const char* p` put it on the value
        // instead, and the two are different promises.
        if (match(TOK_CONST)) base->isConst = true;
    }
    return base;
}

// The builtin types are C's, every one of them, so the whole specifier soup is
// resolved here and the C++ layer inherits it by calling this first.
//
//     [const] { signed | unsigned | short | long | int | char
//               | void | float | double }...
//
// The specifiers may appear in any order and combine, so they are collected
// into three independent facts -- signedness, length, base -- and resolved once
// at the end.  Nothing is consumed unless it is a specifier, which is what lets
// parseStatement() call this speculatively.
Type *Parser::parseType() {
    bool sawConst = match(TOK_CONST);

    enum { SignNone, SignSigned, SignUnsigned } sign = SignNone;
    enum { LenNone, LenShort, LenLong } length = LenNone;
    enum { BaseNone, BaseInt, BaseChar, BaseVoid, BaseFloat, BaseDouble }
        base = BaseNone;

    const int line = cur.line, col = cur.col;
    bool sawAny = false;
    bool bad = false;
    bool alreadyReported = false;       // a specific message beats the generic one

    for (;;) {
        switch (cur.kind) {
        case TOK_SIGNED:   if (sign != SignNone) bad = true; sign = SignSigned; break;
        case TOK_UNSIGNED: if (sign != SignNone) bad = true; sign = SignUnsigned; break;
        case TOK_SHORT:    if (length != LenNone) bad = true; length = LenShort; break;
        case TOK_LONG:
            if (length == LenLong) {
                errorAtCurrent("'long long' is not supported");
                bad = true;
                alreadyReported = true;
            } else if (length != LenNone) bad = true;
            length = LenLong;
            break;
        case TOK_INT:      if (base != BaseNone) bad = true; base = BaseInt; break;
        case TOK_CHAR:     if (base != BaseNone) bad = true; base = BaseChar; break;
        case TOK_VOID:     if (base != BaseNone) bad = true; base = BaseVoid; break;
        case TOK_FLOAT:    if (base != BaseNone) bad = true; base = BaseFloat; break;
        case TOK_DOUBLE:   if (base != BaseNone) bad = true; base = BaseDouble; break;
        default:
            goto resolve;
        }
        sawAny = true;
        advance();
        if (match(TOK_CONST)) sawConst = true;      // const may trail too
    }

resolve:
    if (!sawAny) return 0;                          // not a type this layer knows

    const bool uns = (sign == SignUnsigned);
    BuiltinKind kind = BK_Int;

    if (base == BaseVoid || base == BaseFloat || base == BaseDouble) {
        // These take no length or signedness.
        if (sign != SignNone || length != LenNone) {
            errorAtCurrent(std::string("'") + (base == BaseVoid ? "void" :
                           base == BaseFloat ? "float" : "double")
                           + "' cannot be combined with signed, unsigned, short or long");
            bad = true;
            alreadyReported = true;
        }
        kind = (base == BaseVoid)  ? BK_Void
             : (base == BaseFloat) ? BK_Float
                                   : BK_Double;
    } else if (base == BaseChar) {
        if (length != LenNone) {
            errorAtCurrent("'char' cannot be combined with short or long");
            bad = true;
            alreadyReported = true;
        }
        // Plain char is a distinct type from signed char, as in C++.
        kind = (sign == SignNone) ? BK_Char : (uns ? BK_UChar : BK_SChar);
    } else {
        // int, or a length and signedness with int implied
        if (length == LenShort)     kind = uns ? BK_UShort : BK_Short;
        else if (length == LenLong) kind = uns ? BK_ULong : BK_Long;
        else                        kind = uns ? BK_UInt : BK_Int;
    }

    if (bad && !alreadyReported) errorAtCurrent("conflicting type specifiers");

    Type *t = new BuiltinType(kind);
    t->line = line;
    t->col = col;
    // The const belongs to the value, not to a pointer built on top of it:
    // `const char *s` is a pointer to const char, and s itself may be moved.
    t->isConst = sawConst;
    return parsePointerSuffixes(t);
}

// --- the precedence chain ---------------------------------------------
// Each level parses the one below and loops on its own operators, so reading
// top to bottom is reading the precedence table.

// One more link in the tree the current expression is building.
//
// Refusing to go deeper is not enough on its own: the loops above are still
// looking at the tokens, and `*` is both a dereference and a multiplication,
// so an abandoned `***...p` came straight back as a multiplication chain and
// went on growing the tree one BinaryExpr at a time.  The tokens have to stop
// being an expression, not merely stop being parsed -- so the rest of it is
// consumed here, at the point of refusal, which is the same thing the
// excluded operators do a few lines below and for the same reason.
bool Parser::chainTooDeep() {
    ++chainLinks;
    if (chainLinks <= MaxNesting) return false;
    if (!chainReported) {
        errorAtCurrent("expression nested too deeply");
        chainReported = true;
        while (cur.kind != TOK_SEMI && cur.kind != TOK_RPAREN &&
               cur.kind != TOK_RBRACE && cur.kind != TOK_COMMA &&
               cur.kind != TOK_EOF) advance();
    }
    return true;
}

bool Parser::tooDeep() {
    if (nesting < MaxNesting) return false;
    if (!nestingReported) {
        errorAtCurrent("nested too deeply");
        nestingReported = true;
    }
    return true;
}

Expr *Parser::parseExpression() {
    if (tooDeep()) return 0;
    // The count is one expression's, so it starts again at the outermost one.
    // A nested expression -- an argument, a parenthesised group -- goes on
    // counting into the same total, which is right: it is the same tree.
    const bool outermost = (exprNesting == 0);
    if (outermost) { chainLinks = 0; chainReported = false; }
    ++nesting;
    ++exprNesting;
    Expr *e = parseAssign();
    --exprNesting;
    --nesting;

    // Refusing the expression abandons it partway, and the rest of it is still
    // sitting there for whatever asked for the expression to complain about
    // next.  One mistake costs one line, so the remains go here -- the same
    // skip the excluded operators below make, for the same reason.
    if (outermost && chainReported) {
        while (cur.kind != TOK_SEMI && cur.kind != TOK_RPAREN &&
               cur.kind != TOK_RBRACE && cur.kind != TOK_EOF) advance();
        return e;
    }

    // An operator left out of the subset lands here, where a complete
    // expression has been parsed and something follows it that no rule above
    // accepts.  Naming it costs one message; leaving it produced a complaint
    // about whichever bracket happened to be expected next, which never
    // mentioned the operator the program was reaching for.  None of these can
    // be a separator -- an argument list consumes its own commas in
    // parseCallSuffix -- so seeing one here is always the operator itself.
    if (cur.kind == TOK_QUESTION) {
        errorAtCurrent("the conditional operator '?:' is not supported; "
                       "use an if statement");
        while (cur.kind != TOK_SEMI && cur.kind != TOK_RPAREN &&
               cur.kind != TOK_EOF) advance();
    } else if (cur.kind == TOK_AMP || cur.kind == TOK_PIPE || cur.kind == TOK_CARET) {
        errorAtCurrent("bitwise operators are not supported; "
                       "'<<' and '>>' are the only bit operations");
        while (cur.kind != TOK_SEMI && cur.kind != TOK_RPAREN &&
               cur.kind != TOK_EOF) advance();
    }
    return e;
}

// Loosest, and groups RIGHT: a = b = c is a = (b = c).  Whether the left side
// is assignable is the semantic pass's question, not the grammar's.
Expr *Parser::parseAssign() {
    Expr *left = parseLogicalOr();
    if (!left) return left;
    BinaryOp op;
    switch (cur.kind) {
    case TOK_ASSIGN:    op = BIN_Assign; break;
    case TOK_PLUSEQ:    op = BIN_AddAssign; break;
    case TOK_MINUSEQ:   op = BIN_SubAssign; break;
    case TOK_STAREQ:    op = BIN_MulAssign; break;
    case TOK_SLASHEQ:   op = BIN_DivAssign; break;
    case TOK_PERCENTEQ: op = BIN_ModAssign; break;
    default: return left;
    }
    const int line = cur.line, col = cur.col;
    advance();
    Expr *e = new BinaryExpr(op, left, parseAssign());
    e->line = line; e->col = col;
    return e;
}

Expr *Parser::parseLogicalOr() {
    Expr *left = parseLogicalAnd();
    while (left && cur.kind == TOK_OROR) {
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(BIN_LOr, left, parseLogicalAnd());
        e->line = line; e->col = col;
        left = e;
    }
    return left;
}

Expr *Parser::parseLogicalAnd() {
    Expr *left = parseEquality();
    while (left && cur.kind == TOK_ANDAND) {
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(BIN_LAnd, left, parseEquality());
        e->line = line; e->col = col;
        left = e;
    }
    return left;
}

Expr *Parser::parseEquality() {
    Expr *left = parseRelational();
    while (left && (cur.kind == TOK_EQ || cur.kind == TOK_NE)) {
        const BinaryOp op = (cur.kind == TOK_EQ) ? BIN_EQ : BIN_NE;
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(op, left, parseRelational());
        e->line = line; e->col = col;
        left = e;
    }
    return left;
}

// Shift binds tighter than a comparison and looser than + and -, exactly as
// in C -- so  a << b + c  is  a << (b + c).
Expr *Parser::parseShift() {
    for (Expr *left = parseAddSub(); ; ) {
        BinaryOp op;
        switch (cur.kind) {
        case TOK_SHL: op = BIN_Shl; break;
        case TOK_SHR: op = BIN_Shr; break;
        default: return left;
        }
        if (!left) return left;
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(op, left, parseAddSub());
        e->line = line; e->col = col;
        left = e;
    }
}

Expr *Parser::parseRelational() {
    for (Expr *left = parseShift(); ; ) {
        BinaryOp op;
        switch (cur.kind) {
        case TOK_LT: op = BIN_LT; break;
        case TOK_GT: op = BIN_GT; break;
        case TOK_LE: op = BIN_LE; break;
        case TOK_GE: op = BIN_GE; break;
        default: return left;
        }
        if (!left) return left;
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(op, left, parseShift());
        e->line = line; e->col = col;
        left = e;
    }
}

Expr *Parser::parseAddSub() {
    Expr *left = parseMulDiv();
    while (left && (cur.kind == TOK_PLUS || cur.kind == TOK_MINUS)) {
        const BinaryOp op = (cur.kind == TOK_PLUS) ? BIN_Add : BIN_Sub;
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(op, left, parseMulDiv());
        e->line = line; e->col = col;
        left = e;
    }
    return left;
}

Expr *Parser::parseMulDiv() {
    for (Expr *left = parseUnary(); ; ) {
        BinaryOp op;
        switch (cur.kind) {
        case TOK_STAR:    op = BIN_Mul; break;
        case TOK_SLASH:   op = BIN_Div; break;
        case TOK_PERCENT: op = BIN_Mod; break;
        default: return left;
        }
        if (!left) return left;
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(op, left, parseUnary());
        e->line = line; e->col = col;
        left = e;
    }
}

Expr *Parser::parseUnary() {
    // Every link in every chain passes through here exactly once, whichever
    // loop above is building it: a binary level parses its right operand by
    // descending to this point, and a unary operator reaches it by recursion.
    // So this is the one place that can count the depth of the tree without
    // the seven precedence loops each having to remember to.
    if (chainTooDeep()) return 0;
    UnaryOp op;
    switch (cur.kind) {
    case TOK_MINUS:      op = UN_Neg; break;
    case TOK_NOT:        op = UN_Not; break;
    case TOK_STAR:       op = UN_Deref; break;
    case TOK_AMP:        op = UN_AddrOf; break;
    case TOK_PLUSPLUS:   op = UN_PreInc; break;
    case TOK_MINUSMINUS: op = UN_PreDec; break;
    default: return parsePostfix();
    }
    const int line = cur.line, col = cur.col;
    advance();
    Expr *e = new UnaryExpr(op, parseUnary());      // unary operators nest
    e->line = line; e->col = col;
    return e;
}

// Calls belong to C, member access does not -- so it comes through a virtual
// hook.  One loop handles both in any order, which is what parses p.getX().y
Expr *Parser::parsePostfix() {
    Expr *e = parsePrimary();                       // virtual
    while (e) {
        if (cur.kind == TOK_LPAREN) { e = parseCallSuffix(e); continue; }
        if (cur.kind == TOK_LBRACKET) { e = parseIndexSuffix(e); continue; }
        if (cur.kind == TOK_PLUSPLUS || cur.kind == TOK_MINUSMINUS) {
            const UnaryOp op = (cur.kind == TOK_PLUSPLUS) ? UN_PostInc : UN_PostDec;
            const int line = cur.line, col = cur.col;
            advance();
            Expr *p = new UnaryExpr(op, e);
            p->line = line; p->col = col;
            e = p;
            continue;
        }
        Expr *m = parseMemberSuffix(e);             // virtual; 0 in the C layer
        if (m) { e = m; continue; }
        break;
    }
    return e;
}

Expr *Parser::parseCallSuffix(Expr *callee) {
    const int line = cur.line, col = cur.col;
    advance();                                      // consume '('
    CallExpr *call = new CallExpr(callee);
    call->line = line;
    call->col = col;
    while (cur.kind != TOK_RPAREN && cur.kind != TOK_EOF) {
        Expr *a = parseExpression();
        if (!a) break;
        call->args.push_back(a);
        if (!match(TOK_COMMA)) break;
    }
    expect(TOK_RPAREN, "after call arguments");
    return call;
}

// a[i] means *(a + i).  Building exactly that keeps subscripting and pointer
// arithmetic one feature rather than two.
Expr *Parser::parseIndexSuffix(Expr *base) {
    const int line = cur.line, col = cur.col;
    advance();                                  // consume '['
    Expr *index = parseExpression();
    expect(TOK_RBRACKET, "after a subscript");
    if (!index) return base;
    Expr *e = new IndexExpr(base, index);
    e->line = line; e->col = col;
    return e;
}

Expr *Parser::parsePrimary() {
    const int line = cur.line, col = cur.col;

    // Every literal form is C's, so they all live in this layer.
    if (cur.kind == TOK_NUMBER || cur.kind == TOK_CHARLIT) {
        const BuiltinKind k = (cur.kind == TOK_CHARLIT) ? BK_Char : BK_Int;
        const long v = cur.numberValue;
        advance();
        Expr *e = new NumberExpr(v, k);
        e->line = line; e->col = col;
        return e;
    }
    if (cur.kind == TOK_FLOATLIT) {
        const double v = cur.floatValue;
        const BuiltinKind k = cur.isFloatSuffixed ? BK_Float : BK_Double;
        advance();
        Expr *e = new FloatExpr(v, k);
        e->line = line; e->col = col;
        return e;
    }
    if (cur.kind == TOK_STRINGLIT) {
        const std::string v = cur.text;
        advance();
        Expr *e = new StringExpr(v);
        e->line = line; e->col = col;
        return e;
    }
    if (cur.kind == TOK_IDENTIFIER) {
        const std::string n = cur.text;
        advance();
        Expr *e = new IdentExpr(n);
        e->line = line; e->col = col;
        return e;
    }
    if (cur.kind == TOK_LPAREN) return parseCastOrParen();

    if (cur.kind == TOK_RESERVED) {
        const char *help = reservedWordHelp(cur.text);
        errorAtCurrent(help ? help : "this keyword is not supported");
        advance();
        // sizeof(T) and the named casts carry an operand; stepping over it
        // keeps one message from becoming three.
        skipParenGroup();
        if (cur.kind == TOK_LT) {           // static_cast<T>(v)
            while (cur.kind != TOK_EOF && cur.kind != TOK_GT) advance();
            match(TOK_GT);
            skipParenGroup();
        }
        return 0;
    }
    errorAtCurrent(std::string("expected an expression, found ") + tokenName(cur.kind));
    return 0;
}

// '(' is ambiguous: it opens either a cast or a parenthesised expression, and
// only what follows tells them apart.  Try the type rule and rewind when it
// does not fit -- the same speculation parseStatement() uses.
Expr *Parser::parseCastOrParen() {
    const int line = cur.line, col = cur.col;
    State st = save();
    advance();                                      // consume '('
    Type *t = parseType();                          // virtual: knows C++ types
    if (t && cur.kind == TOK_RPAREN) {
        advance();
        Expr *inner = parseUnary();                 // a cast binds like a unary
        Expr *e = new CastExpr(t, inner);
        e->line = line; e->col = col;
        return e;
    }
    delete t;
    restore(st);

    advance();                                      // consume '(' again
    Expr *e = parseExpression();
    // A comma HERE is the comma operator: an argument list consumes its own
    // commas in parseCallSuffix, so this pair of brackets is grouping and
    // nothing else.
    if (cur.kind == TOK_COMMA) {
        errorAtCurrent("the comma operator is not supported; "
                       "write the two expressions as separate statements");
        while (cur.kind != TOK_RPAREN && cur.kind != TOK_SEMI &&
               cur.kind != TOK_EOF) advance();
    }
    expect(TOK_RPAREN, "after parenthesised expression");
    return e;
}

// This subset introduces member access with classes, in the layer above.
Expr *Parser::parseMemberSuffix(Expr *) {
    return 0;
}

void Parser::parseFunctionTail(Function *) {
}

} // namespace cc
