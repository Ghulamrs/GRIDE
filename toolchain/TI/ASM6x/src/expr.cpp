#include "asm.h"

/* Expressions as asm6x reads them: numbers, symbols, $ for the current address, + - * / %
   << >> & | ^ with ~ and unary minus, parentheses, and TI's relocation operators -
   $EXIDX_FUNC(f), $EXIDX_EXTAB("t"), $EXTAB_LP(l), $EXTAB_RTTI(t), $EXTAB_SCOPE(l) - each of
   which is the symbol's address wearing the relocation the linker must apply to it
   (PREL31, PREL31, PREL31, EHTYPE, and none: a scope is a plain address, and a difference
   of two scopes is a constant). A value is a constant, or one symbol plus a constant, or
   the difference of two labels, or $ plus a constant; anything else is refused, as asm6x
   refuses it. Precedence is C's: * / % before + -, then the shifts, then & ^ |. */

namespace {

struct Parser {
    Unit &u;
    const std::vector<Token> &t;
    size_t i, end;
    std::string err;
    bool forward;

    Parser(Unit &unit, const std::vector<Token> &tokens, size_t from, size_t to)
        : u(unit), t(tokens), i(from), end(to), forward(false) {}

    bool fail(const std::string &m) { if (err.empty()) err = m; return false; }
    bool at(char c) const { return i < end && is_punct(t, i, c); }
    bool at2(const char *s) const { return i < end && t[i].kind == T_PUNCT && t[i].text == s; }

    static bool constant(const Value &v) { return v.sym < 0 && v.sub < 0 && v.secref < 0; }

    bool primary(Value &v) {
        v.v = 0; v.sym = -1; v.sub = -1; v.secref = -1; v.op = R_NONE; v.operate = false; v.forward = false;
        if (i >= end) return fail("an expression is missing");
        if (at('(')) {
            i++;
            if (!sum(v)) return false;
            if (!at(')')) return fail("')' expected");
            i++;
            return true;
        }
        if (at('-')) {
            i++;
            if (!primary(v)) return false;
            if (!constant(v)) return fail("a label cannot be negated");
            v.v = -v.v;
            return true;
        }
        if (at('~')) {
            i++;
            if (!primary(v)) return false;
            if (!constant(v)) return fail("a label cannot be complemented");
            v.v = ~v.v;
            return true;
        }
        if (at('+')) { i++; return primary(v); }
        if (t[i].kind == T_NUM) { v.v = t[i].value; i++; return true; }
        if (t[i].kind == T_STR) {
            /* a one-character string is its character, as asm6x takes .word "a" */
            if (t[i].text.size() != 1) return fail("a string is not a value here");
            v.v = (unsigned char)t[i].text[0]; i++; return true;
        }
        if (t[i].kind != T_NAME) return fail("a value expected, not '" + t[i].text + "'");
        const std::string &name = t[i].text;
        if (name == "$") {
            v.secref = u.current;
            v.v = (long long)u.here();
            i++;
            return true;
        }
        if (name.size() > 1 && name[0] == '$' && i + 1 < end && is_punct(t, i + 1, '(')) {
            std::string op = upper(name);
            RelKind kind;
            if (op == "$EXIDX_FUNC" || op == "$EXIDX_EXTAB" || op == "$EXTAB_LP") kind = R_PREL31;
            else if (op == "$EXTAB_RTTI") kind = R_EHTYPE;
            else if (op == "$EXTAB_SCOPE") kind = R_NONE;
            else return fail("'" + name + "' is not an operator this assembler knows");
            i += 2;
            if (i >= end || (t[i].kind != T_NAME && t[i].kind != T_STR)) return fail(name + " needs a symbol");
            v.sym = u.ref(t[i].text);
            u.symbols[v.sym].referenced = true;
            i++;
            if (!at(')')) return fail("')' expected after " + name);
            i++;
            v.op = kind;
            v.operate = true;
            return true;
        }
        v.sym = u.ref(name);
        u.symbols[v.sym].referenced = true;
        i++;
        /* a .set constant stands for its value; a .set alias of a label for the label */
        Symbol &s = u.symbols[v.sym];
        if (s.defined && s.section < 0) {
            if (s.alias >= 0) { v.v = s.aliasAdd; v.sym = s.alias; u.symbols[s.alias].referenced = true; }
            else { v.v = s.value; v.sym = -1; }
            if (s.pass != u.pass) forward = true;
        }
        return true;
    }

    bool product(Value &v) {
        if (!primary(v)) return false;
        while (i < end && (at('*') || at('/') || at('%'))) {
            char op = t[i].text[0];
            i++;
            Value r;
            if (!primary(r)) return false;
            if (!constant(v) || !constant(r)) return fail("a label cannot be multiplied or divided");
            if (op != '*' && r.v == 0) return fail("division by zero");
            v.v = op == '*' ? v.v * r.v : op == '/' ? v.v / r.v : v.v % r.v;
        }
        return true;
    }

    bool sum(Value &v) {
        if (!product(v)) return false;
        while (i < end && (at('+') || at('-'))) {
            bool add = at('+');
            i++;
            Value r;
            if (!product(r)) return false;
            if (v.operate && r.operate && v.op != r.op) return fail("two different operators in one expression");
            if (add) {
                if ((v.sym >= 0 || v.secref >= 0) && (r.sym >= 0 || r.secref >= 0)) return fail("two addresses cannot be added");
                if (r.sym >= 0) { v.sym = r.sym; v.op = r.op; v.operate = r.operate; }
                if (r.secref >= 0) v.secref = r.secref;
                v.v += r.v;
            } else {
                if (r.secref >= 0) return fail("$ cannot be subtracted");
                if (r.sym >= 0) {
                    if (v.sym < 0) return fail("a label cannot be subtracted from a constant");
                    if (v.sub >= 0) return fail("only two labels can be differenced");
                    v.sub = r.sym;
                }
                v.v -= r.v;
            }
        }
        return true;
    }

    bool shift(Value &v) {
        if (!sum(v)) return false;
        while (i < end && (at2("<<") || at2(">>"))) {
            bool left = at2("<<");
            i++;
            Value r;
            if (!sum(r)) return false;
            if (!constant(v) || !constant(r)) return fail("a label cannot be shifted");
            if (r.v < 0 || r.v > 63) return fail("a shift count is 0 to 63");
            v.v = left ? (long long)((unsigned long long)v.v << r.v) : v.v >> r.v;
        }
        return true;
    }

    bool bits(Value &v) {
        if (!shift(v)) return false;
        while (i < end && (at('&') || at('^') || at('|'))) {
            char op = t[i].text[0];
            i++;
            Value r;
            if (!shift(r)) return false;
            if (!constant(v) || !constant(r)) return fail("a label cannot be masked");
            v.v = op == '&' ? (v.v & r.v) : op == '^' ? (v.v ^ r.v) : (v.v | r.v);
        }
        return true;
    }
};

}

bool eval(Unit &u, const std::vector<Token> &t, size_t from, size_t to, Value &v, std::string &err)
{
    Parser p(u, t, from, to);
    if (!p.bits(v)) { err = p.err; return false; }
    if (p.i != to) { err = "unexpected '" + t[p.i].text + "' in an expression"; return false; }
    v.forward = p.forward;
    /* a difference of two placed labels of one section is a constant */
    if (v.sym >= 0 && v.sub >= 0) {
        const Symbol &a = u.symbols[v.sym], &b = u.symbols[v.sub];
        if (a.defined && b.defined && a.section == b.section && a.section >= 0 && !u.compressing) {
            v.v += a.value - b.value;
            v.sym = -1; v.sub = -1;
            v.op = R_NONE; v.operate = false;
        }
    }
    return true;
}

/* a constant; with settled, one known by this point of the file, as the directives that
   size a section need (asm6x: "Absolute, well-defined integer value expected") */
bool eval_const(Unit &u, const std::vector<Token> &t, size_t from, size_t to, long long &v, std::string &err, bool settled)
{
    Value x;
    if (!eval(u, t, from, to, x, err)) return false;
    if (x.sym >= 0 || x.sub >= 0 || x.secref >= 0) {
        if (u.pass > 1 || settled || (x.sym >= 0 && u.symbols[x.sym].defined)) { err = "a constant is needed here, not a label"; return false; }
        v = 0;   /* a forward label in the first pass: the value settles next pass */
        return true;
    }
    if (settled && x.forward) { err = "a constant defined earlier in the file is needed here"; return false; }
    v = x.v;
    return true;
}
