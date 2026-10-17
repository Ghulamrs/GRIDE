// Lexer.cpp
//
// C++98 only.

#include "Lexer.h"

#include "Diagnostics.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <string>

// --- token spelling, for "expected X" messages -------------------------
const char *tokenName(TokenKind k) {
    switch (k) {
    case TOK_EOF:        return "end of file";
    case TOK_IDENTIFIER: return "identifier";
    case TOK_NUMBER:     return "number";
    case TOK_FLOATLIT:   return "floating literal";
    case TOK_CHARLIT:    return "character literal";
    case TOK_STRINGLIT:  return "string literal";
    case TOK_INT:        return "int";
    case TOK_CHAR:       return "char";
    case TOK_VOID:       return "void";
    case TOK_BOOL:       return "bool";
    case TOK_SHORT:      return "short";
    case TOK_LONG:       return "long";
    case TOK_SIGNED:     return "signed";
    case TOK_UNSIGNED:   return "unsigned";
    case TOK_FLOAT:      return "float";
    case TOK_DOUBLE:     return "double";
    case TOK_CONST:      return "const";
    case TOK_RETURN:     return "return";
    case TOK_IF:         return "if";
    case TOK_ELSE:       return "else";
    case TOK_WHILE:      return "while";
    case TOK_FOR:        return "for";
    case TOK_BREAK:      return "break";
    case TOK_CONTINUE:   return "continue";
    case TOK_CLASS:      return "class";
    case TOK_STRUCT:     return "struct";
    case TOK_PUBLIC:     return "public";
    case TOK_PRIVATE:    return "private";
    case TOK_PROTECTED:  return "protected";
    case TOK_VIRTUAL:    return "virtual";
    case TOK_NEW:        return "new";
    case TOK_DELETE:     return "delete";
    case TOK_THIS:       return "this";
    case TOK_TRUE:       return "true";
    case TOK_FALSE:      return "false";
    case TOK_SEMI:       return "';'";
    case TOK_LPAREN:     return "'('";
    case TOK_RPAREN:     return "')'";
    case TOK_LBRACE:     return "'{'";
    case TOK_RBRACE:     return "'}'";
    case TOK_LBRACKET:   return "'['";
    case TOK_RBRACKET:   return "']'";
    case TOK_COMMA:      return "','";
    case TOK_COLON:      return "':'";
    case TOK_COLONCOLON: return "'::'";
    case TOK_DOT:        return "'.'";
    case TOK_ELLIPSIS:   return "'...'";
    case TOK_SHL:        return "'<<'";
    case TOK_SHR:        return "'>>'";
    case TOK_HASH:       return "'#'";
    case TOK_ARROW:      return "'->'";
    case TOK_TILDE:      return "'~'";
    case TOK_PLUS:       return "'+'";
    case TOK_MINUS:      return "'-'";
    case TOK_STAR:       return "'*'";
    case TOK_SLASH:      return "'/'";
    case TOK_PERCENT:    return "'%'";
    case TOK_ASSIGN:     return "'='";
    case TOK_PLUSPLUS:   return "'++'";
    case TOK_MINUSMINUS: return "'--'";
    case TOK_PLUSEQ:     return "'+='";
    case TOK_MINUSEQ:    return "'-='";
    case TOK_STAREQ:     return "'*='";
    case TOK_SLASHEQ:    return "'/='";
    case TOK_PERCENTEQ:  return "'%='";
    case TOK_DO:         return "do";
    case TOK_SWITCH:     return "switch";
    case TOK_CASE:       return "case";
    case TOK_DEFAULT:    return "default";
    case TOK_EQ:         return "'=='";
    case TOK_NE:         return "'!='";
    case TOK_LT:         return "'<'";
    case TOK_GT:         return "'>'";
    case TOK_LE:         return "'<='";
    case TOK_GE:         return "'>='";
    case TOK_ANDAND:     return "'&&'";
    case TOK_OROR:       return "'||'";
    case TOK_NOT:        return "'!'";
    case TOK_AMP:        return "'&'";
    case TOK_QUESTION:   return "'?'";
    case TOK_PIPE:       return "'|'";
    case TOK_CARET:      return "'^'";
    case TOK_RESERVED:   return "a reserved keyword";
    case TOK_UNKNOWN:    return "unknown token";
    }
    return "token";
}

// The features this subset deliberately leaves out, each with the message the
// parser gives when it meets one.  Naming the feature and the alternative is
// the difference between a compiler that teaches and one that merely refuses.
const char *reservedWordHelp(const std::string &w) {
    if (w == "template" || w == "typename" || w == "export") {
        return "templates are not supported";
    }
    if (w == "throw" || w == "try" || w == "catch") {
        return "exceptions are not supported";
    }
    if (w == "namespace" || w == "using") {
        return "namespaces are not supported";
    }
    if (w == "operator") {
        return "operator overloading is not supported";
    }
    if (w == "static") return "'static' is not supported";
    if (w == "friend")   return "'friend' is not supported";
    if (w == "mutable")  return "'mutable' is not supported";
    if (w == "explicit") return "'explicit' is not supported";
    if (w == "inline")   return "'inline' is not supported";
    if (w == "goto")     return "'goto' is not supported";
    if (w == "sizeof")   return "'sizeof' is not supported";
    if (w == "enum")     return "'enum' is not supported";
    if (w == "union")    return "'union' is not supported";
    if (w == "typedef")  return "'typedef' is not supported";
    if (w == "static_cast" || w == "const_cast" ||
        w == "dynamic_cast" || w == "reinterpret_cast") {
        return "named casts are not supported; use (T)value";
    }
    if (w == "volatile" || w == "register" || w == "extern" || w == "auto") {
        return "storage-class keywords are not supported";
    }
    if (w == "wchar_t")  return "'wchar_t' is not supported";
    if (w == "asm")      return "assembly is not supported";
    return 0;
}

// One place for the escapes both literal forms share.
static char decodeEscape(char c) {
    switch (c) {
    case 'n':  return '\n';
    case 't':  return '\t';
    case 'r':  return '\r';
    case '0':  return '\0';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"':  return '"';
    default:   return c;
    }
}

// --- position bookkeeping ---------------------------------------------

char Lexer::get() {
    if (pos >= src.size()) return '\0';
    char c = src[pos++];
    if (c == '\n') { ++line; col = 1; }
    else           { ++col; }
    return c;
}

Lexer::Position Lexer::tell() const {
    Position p;
    p.offset = pos;
    p.line = line;
    p.col = col;
    return p;
}

void Lexer::seek(const Position &p) {
    pos = p.offset;
    line = p.line;
    col = p.col;
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        while (std::isspace(static_cast<unsigned char>(peek()))) get();
        if (peek() == '/' && peekAt(1) == '/') {
            while (peek() != '\0' && peek() != '\n') get();
            continue;
        }
        if (peek() == '/' && peekAt(1) == '*') {
            get(); get();                       // consume the opening /*
            while (peek() != '\0') {
                if (peek() == '*' && peekAt(1) == '/') {
                    get(); get();               // consume the closing */
                    break;
                }
                get();
            }
            continue;
        }
        return;
    }
}

Token Lexer::makeToken(TokenKind k, int startLine, int startCol) {
    Token t;
    t.kind = k;
    t.line = startLine;
    t.col = startCol;
    return t;
}

// --- the scanner ------------------------------------------------------

Token Lexer::nextToken() {
    skipWhitespaceAndComments();

    const int startLine = line;
    const int startCol = col;

    char c = peek();
    if (c == '\0') return makeToken(TOK_EOF, startLine, startCol);

    // identifier or keyword
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        std::string id;
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') id += get();
        Token tok = makeToken(TOK_IDENTIFIER, startLine, startCol);
        if      (id == "int")       tok.kind = TOK_INT;
        else if (id == "char")      tok.kind = TOK_CHAR;
        else if (id == "void")      tok.kind = TOK_VOID;
        else if (id == "bool")      tok.kind = TOK_BOOL;
        else if (id == "short")     tok.kind = TOK_SHORT;
        else if (id == "long")      tok.kind = TOK_LONG;
        else if (id == "signed")    tok.kind = TOK_SIGNED;
        else if (id == "unsigned")  tok.kind = TOK_UNSIGNED;
        else if (id == "float")     tok.kind = TOK_FLOAT;
        else if (id == "double")    tok.kind = TOK_DOUBLE;
        else if (id == "const")     tok.kind = TOK_CONST;
        else if (id == "return")    tok.kind = TOK_RETURN;
        else if (id == "if")        tok.kind = TOK_IF;
        else if (id == "else")      tok.kind = TOK_ELSE;
        else if (id == "while")     tok.kind = TOK_WHILE;
        else if (id == "for")       tok.kind = TOK_FOR;
        else if (id == "break")     tok.kind = TOK_BREAK;
        else if (id == "continue")  tok.kind = TOK_CONTINUE;
        else if (id == "do")        tok.kind = TOK_DO;
        else if (id == "switch")    tok.kind = TOK_SWITCH;
        else if (id == "case")      tok.kind = TOK_CASE;
        else if (id == "default")   tok.kind = TOK_DEFAULT;
        else if (id == "class")     tok.kind = TOK_CLASS;
        else if (id == "struct")    tok.kind = TOK_STRUCT;
        else if (id == "public")    tok.kind = TOK_PUBLIC;
        else if (id == "private")   tok.kind = TOK_PRIVATE;
        else if (id == "protected") tok.kind = TOK_PROTECTED;
        else if (id == "virtual")   tok.kind = TOK_VIRTUAL;
        else if (id == "new")       tok.kind = TOK_NEW;
        else if (id == "delete")    tok.kind = TOK_DELETE;
        else if (id == "this")      tok.kind = TOK_THIS;
        else if (id == "true")      tok.kind = TOK_TRUE;
        else if (id == "false")     tok.kind = TOK_FALSE;
        else if (reservedWordHelp(id)) { tok.kind = TOK_RESERVED; tok.text = id; }
        else                        tok.text = id;
        return tok;
    }

    // A number is integer until a '.' or an exponent proves otherwise.  The
    // '.' is only part of the number when a digit follows, so that  p.x  still
    // lexes as three tokens.
    if (std::isdigit(static_cast<unsigned char>(c))) {
        std::string num;

        // 0x... and 0... are integers in a different base.  Without this they
        // lexed as `0` followed by an identifier, and the errors that followed
        // never mentioned the number.
        if (peek() == '0' && (peekAt(1) == 'x' || peekAt(1) == 'X')) {
            num += get();                                   // '0'
            num += get();                                   // 'x'
            while (std::isxdigit(static_cast<unsigned char>(peek()))) num += get();
            Token hex = makeToken(TOK_NUMBER, startLine, startCol);
            hex.text = num;
            hex.numberValue = std::strtol(num.c_str(), 0, 16);
            return hex;
        }
        if (peek() == '0' && peekAt(1) >= '0' && peekAt(1) <= '7') {
            while (peek() >= '0' && peek() <= '7') num += get();
            Token oct = makeToken(TOK_NUMBER, startLine, startCol);
            oct.text = num;
            oct.numberValue = std::strtol(num.c_str(), 0, 8);
            return oct;
        }

        while (std::isdigit(static_cast<unsigned char>(peek()))) num += get();

        bool isFloat = false;
        if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekAt(1)))) {
            isFloat = true;
            num += get();                                   // the '.'
            while (std::isdigit(static_cast<unsigned char>(peek()))) num += get();
        }
        if (peek() == 'e' || peek() == 'E') {
            const char sign = peekAt(1);
            const bool signedExp = (sign == '+' || sign == '-');
            if (std::isdigit(static_cast<unsigned char>(signedExp ? peekAt(2) : sign))) {
                isFloat = true;
                num += get();                               // 'e'
                if (signedExp) num += get();
                while (std::isdigit(static_cast<unsigned char>(peek()))) num += get();
            }
        }

        if (isFloat) {
            Token tok = makeToken(TOK_FLOATLIT, startLine, startCol);
            tok.text = num;
            tok.floatValue = std::atof(num.c_str());
            if (peek() == 'f' || peek() == 'F') { get(); tok.isFloatSuffixed = true; }
            return tok;
        }
        Token tok = makeToken(TOK_NUMBER, startLine, startCol);
        tok.text = num;
        tok.numberValue = std::atol(num.c_str());
        return tok;
    }

    // 'A' and "text".  Escapes are shared, so they are decoded in one place.
    if (c == '\'' || c == '"') {
        const char quote = get();
        std::string body;
        bool closed = false;
        while (peek() != '\0') {
            if (peek() == quote) { get(); closed = true; break; }
            if (peek() == '\n') break;                       // unterminated
            char ch = get();
            if (ch == '\\') ch = decodeEscape(get());
            body += ch;
        }
        Token tok = makeToken(quote == '\'' ? TOK_CHARLIT : TOK_STRINGLIT,
                              startLine, startCol);
        if (!closed) {
            tok.kind = TOK_UNKNOWN;
            tok.text = "unterminated literal";
            return tok;
        }
        if (quote == '"') { tok.text = body; return tok; }
        tok.numberValue = body.empty() ? 0 : static_cast<unsigned char>(body[0]);
        tok.text = body;
        return tok;
    }

    // punctuation and operators.  Two-character forms are tested before the
    // one-character form they start with, or  ==  would lex as  = =.
    char punct = get();
    TokenKind kind = TOK_UNKNOWN;
    switch (punct) {
    case ';': kind = TOK_SEMI; break;
    case '(': kind = TOK_LPAREN; break;
    case ')': kind = TOK_RPAREN; break;
    case '{': kind = TOK_LBRACE; break;
    case '}': kind = TOK_RBRACE; break;
    case '[': kind = TOK_LBRACKET; break;
    case ']': kind = TOK_RBRACKET; break;
    case ',': kind = TOK_COMMA; break;
    case '~': kind = TOK_TILDE; break;
    case '+':
        if (peek() == '+')      { get(); kind = TOK_PLUSPLUS; }
        else if (peek() == '=') { get(); kind = TOK_PLUSEQ; }
        else kind = TOK_PLUS;
        break;
    case '*':
        if (peek() == '=') { get(); kind = TOK_STAREQ; }
        else kind = TOK_STAR;
        break;
    case '/':
        if (peek() == '=') { get(); kind = TOK_SLASHEQ; }
        else kind = TOK_SLASH;
        break;
    case '%':
        if (peek() == '=') { get(); kind = TOK_PERCENTEQ; }
        else kind = TOK_PERCENT;
        break;
    case '.':
        // '...' is one token, so a variadic parameter list can be named.
        if (peek() == '.' && peekAt(1) == '.') { get(); get(); kind = TOK_ELLIPSIS; }
        else kind = TOK_DOT;
        break;
    case '#': kind = TOK_HASH; break;
    case '-':
        if (peek() == '>')      { get(); kind = TOK_ARROW; }
        else if (peek() == '-') { get(); kind = TOK_MINUSMINUS; }
        else if (peek() == '=') { get(); kind = TOK_MINUSEQ; }
        else kind = TOK_MINUS;
        break;
    case '=':
        if (peek() == '=') { get(); kind = TOK_EQ; }
        else kind = TOK_ASSIGN;
        break;
    case '!':
        if (peek() == '=') { get(); kind = TOK_NE; }
        else kind = TOK_NOT;
        break;
    case '<':
        if (peek() == '=')      { get(); kind = TOK_LE; }
        else if (peek() == '<') { get(); kind = TOK_SHL; }
        else kind = TOK_LT;
        break;
    case '>':
        if (peek() == '=')      { get(); kind = TOK_GE; }
        else if (peek() == '>') { get(); kind = TOK_SHR; }
        else kind = TOK_GT;
        break;
    case '&':
        if (peek() == '&') { get(); kind = TOK_ANDAND; }
        else kind = TOK_AMP;
        break;
    case '|':
        if (peek() == '|') { get(); kind = TOK_OROR; }
        else kind = TOK_PIPE;
        break;
    case '?': kind = TOK_QUESTION; break;
    case '^': kind = TOK_CARET; break;
    case ':':
        if (peek() == ':') { get(); kind = TOK_COLONCOLON; }
        else kind = TOK_COLON;
        break;
    default:
        kind = TOK_UNKNOWN;
        break;
    }

    Token tok = makeToken(kind, startLine, startCol);
    if (kind == TOK_UNKNOWN) tok.text = std::string(1, punct);
    return tok;
}

// A factory, so the parsers never need the lexer's definition in their headers.
Lexer *createLexer(const std::string &s) {
    return new Lexer(s);
}

// --- object-like macros ----------------------------------------------------
//
// A #define is a textual substitution, so it happens before the first token
// exists.  Everything else about the language is unaffected -- which is the
// point: a constant should not need a rule in the grammar.

namespace {

bool identStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool identPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Copies a string or character literal through untouched: PI inside "PI" is
// not a macro use.
void copyLiteral(const std::string &src, std::size_t &i, std::string &out) {
    const char quote = src[i];
    out += src[i++];
    while (i < src.size()) {
        if (src[i] == '\\' && i + 1 < src.size()) { out += src[i++]; out += src[i++]; continue; }
        out += src[i];
        if (src[i] == quote) { ++i; return; }
        if (src[i] == '\n') { ++i; return; }             // unterminated
        ++i;
    }
}

} // namespace

std::string expandDefines(const std::string &src, Diagnostics &diag) {
    std::map<std::string, std::string> macros;
    std::string out;
    out.reserve(src.size());

    std::size_t i = 0;
    int line = 1;
    bool blank = true;                  // nothing but spaces on this line yet

    while (i < src.size()) {
        const char c = src[i];

        if (c == '\n') { out += c; ++i; ++line; blank = true; continue; }

        // Comments and literals pass through: a macro name inside one is text.
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') out += src[i++];
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            out += src[i++]; out += src[i++];
            while (i < src.size() && !(src[i] == '*' && i + 1 < src.size() && src[i + 1] == '/')) {
                if (src[i] == '\n') ++line;
                out += src[i++];
            }
            if (i < src.size()) { out += src[i++]; out += src[i++]; }
            blank = false;
            continue;
        }
        if (c == '"' || c == '\'') { copyLiteral(src, i, out); blank = false; continue; }

        // A directive, but only when it opens the line.
        if (c == '#' && blank) {
            std::size_t j = i + 1;
            while (j < src.size() && (src[j] == ' ' || src[j] == '\t')) ++j;
            std::string word;
            while (j < src.size() && identPart(src[j])) word += src[j++];

            if (word != "define") {
                // #include and the rest still reach the parser, which names
                // whichever one it is.
                while (i < src.size() && src[i] != '\n') out += src[i++];
                continue;
            }

            while (j < src.size() && (src[j] == ' ' || src[j] == '\t')) ++j;
            std::string name;
            while (j < src.size() && identPart(src[j])) name += src[j++];

            if (name.empty()) {
                diag.error(line, 1, "'#define' needs a name");
            } else if (j < src.size() && src[j] == '(') {
                diag.error(line, 1, "function-like macros are not supported");
            } else {
                std::string body;
                while (j < src.size() && src[j] != '\n') body += src[j++];
                // Trim, then expand through macros already defined, so one
                // constant may be written in terms of another.
                std::size_t b = 0, e = body.size();
                while (b < e && (body[b] == ' ' || body[b] == '\t')) ++b;
                while (e > b && (body[e - 1] == ' ' || body[e - 1] == '\t' || body[e - 1] == '\r')) --e;
                body = body.substr(b, e - b);

                std::string expanded;
                std::size_t k = 0;
                while (k < body.size()) {
                    // A literal in the body is text, exactly as it is anywhere
                    // else: #define G "PI" does not mention the macro PI.
                    if (body[k] == '"' || body[k] == '\'') {
                        copyLiteral(body, k, expanded);
                        continue;
                    }
                    if (identStart(body[k])) {
                        std::string w;
                        while (k < body.size() && identPart(body[k])) w += body[k++];
                        std::map<std::string, std::string>::const_iterator m = macros.find(w);
                        expanded += (m == macros.end()) ? w : m->second;
                        continue;
                    }
                    expanded += body[k++];
                }
                macros[name] = expanded;
            }

            // The line is blanked, not deleted, so every later line keeps its
            // number and a diagnostic still points where the user is looking.
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }

        if (identStart(c)) {
            std::string word;
            const std::size_t start = i;
            while (i < src.size() && identPart(src[i])) word += src[i++];
            std::map<std::string, std::string>::const_iterator m = macros.find(word);
            out += (m == macros.end()) ? word : m->second;
            (void)start;
            blank = false;
            continue;
        }

        if (c != ' ' && c != '\t' && c != '\r') blank = false;
        out += c;
        ++i;
    }
    return out;
}


// --- <iostream>, in the language itself --------------------------------------
//
// ostream holds nothing: every one of its operators forwards to a native and
// returns *this, which is what makes  cout << a << b  chain.  endl is an
// object of its own class, so that `cout << endl` picks an overload rather
// than needing a special rule.

namespace {

const char *IOStreamPrelude =
    "class __endl_t { public: int _; __endl_t() { _ = 0; } };"
    " __endl_t endl;"
    " class ostream {"
    " public: int _;"
    " ostream() { _ = 0; }"
    " ostream operator<<(int n)      { print_int(n); return *this; }"
    " ostream operator<<(long n)     { print_int(n); return *this; }"
    " ostream operator<<(short n)    { print_int(n); return *this; }"
    " ostream operator<<(double d)   { print_double(d); return *this; }"
    " ostream operator<<(float f)    { print_double(f); return *this; }"
    " ostream operator<<(char c)     { print_char(c); return *this; }"
    " ostream operator<<(char* s)    { print_string(s); return *this; }"
    // Any other pointer prints as an address.  Without this one the only
    // overload a pointer could reach was the bool, and `cout << p` printed 1.
    " ostream operator<<(void* p)    { print_pointer(p); return *this; }"
    " ostream operator<<(bool b)     { print_int(b); return *this; }"
    " ostream operator<<(__endl_t e) { print_line(); return *this; }"
    " };"
    " ostream cout;"
    // istream is the same shape: no state of its own, every operator forwards
    // to a native and returns *this so that  cin >> a >> b  chains.  Whether
    // the last read worked lives in the machine rather than in the object,
    // which is why a copy returned by value costs nothing -- and why
    // cin.good() is right after a chain, not just after its first read.
    " class istream {"
    " public: int _;"
    " istream() { _ = 0; }"
    // A read that fails leaves its destination alone, which is what C++98
    // says -- so every one of these asks whether the read worked before it
    // assigns.  There are no exceptions here to throw instead.
    " istream operator>>(int &n)    { long v = read_int(); if (input_good() != 0) n = (int) v; return *this; }"
    " istream operator>>(long &n)   { long v = read_int(); if (input_good() != 0) n = v; return *this; }"
    " istream operator>>(short &n)  { long v = read_int(); if (input_good() != 0) n = (short) v; return *this; }"
    " istream operator>>(double &d) { double v = read_double(); if (input_good() != 0) d = v; return *this; }"
    " istream operator>>(float &f)  { double v = read_double(); if (input_good() != 0) f = (float) v; return *this; }"
    " istream operator>>(char &c)   { int v = read_char(); if (input_good() != 0) c = (char) v; return *this; }"
    " istream operator>>(bool &b)   { long v = read_int(); if (input_good() != 0) b = v != 0; return *this; }"
    // No width, so the buffer has to say how long it is: one from new[] does,
    // and the machine asks it.  Anything else gets told to use getline rather
    // than being written past the end of.
    " istream operator>>(char* s)   { read_string(s, 0); return *this; }"
    " bool good() { return input_good() != 0; }"
    " bool eof() { return input_good() == 0; }"
    " void getline(char* s, int max) { read_line(s, max); }"
    " };"
    " istream cin;"
    " class errstream {"
    " public: int _;"
    " errstream() { _ = 0; }"
    " errstream operator<<(int n)      { err_int(n); return *this; }"
    " errstream operator<<(long n)     { err_int(n); return *this; }"
    " errstream operator<<(short n)    { err_int(n); return *this; }"
    " errstream operator<<(double d)   { err_double(d); return *this; }"
    " errstream operator<<(float f)    { err_double(f); return *this; }"
    " errstream operator<<(char c)     { err_char(c); return *this; }"
    " errstream operator<<(char* s)    { err_string(s); return *this; }"
    " errstream operator<<(void* p)    { err_pointer(p); return *this; }"
    " errstream operator<<(bool b)     { err_int(b); return *this; }"
    " errstream operator<<(__endl_t e) { err_line(); return *this; }"
    " };"
    " errstream cerr;";

// The natives the prelude leans on.  Declaring them here means a program that
// includes <iostream> need not declare them itself.
const char *IOStreamNatives =
    "void print_int(long n);"
    " void print_char(int c);"
    " void print_double(double d);"
    " void print_string(char* s);"
    " void print_line();"
    " void err_int(long n);"
    " void err_char(int c);"
    " void err_double(double d);"
    " void err_string(char* s);"
    " void err_line();"
    " void print_pointer(void* p);"
    " void err_pointer(void* p);"
    " long read_int();"
    " double read_double();"
    " int read_char();"
    " void read_string(char* s, int max);"
    " void read_line(char* s, int max);"
    " int input_good();";

// Does the source include the named header?  Only the spelling matters --
// there is no file to look for.
bool includesHeader(const std::string &src, const std::string &name) {
    // Comments and literals are skipped: `// #include <iostream>` mentions the
    // header, it does not include it, and injecting the prelude on the strength
    // of a comment puts names into the program nobody asked for.
    std::size_t i = 0;
    bool blank = true;                  // nothing but spaces on this line yet
    while (i < src.size()) {
        const char c = src[i];
        if (c == '\n') { ++i; blank = true; continue; }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) ++i;
            i = (i + 1 < src.size()) ? i + 2 : src.size();
            blank = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            const char quote = c;
            ++i;
            while (i < src.size() && src[i] != quote && src[i] != '\n') {
                if (src[i] == '\\' && i + 1 < src.size()) ++i;
                ++i;
            }
            if (i < src.size()) ++i;
            blank = false;
            continue;
        }
        if (c == '#' && blank) {
            const std::size_t eol = src.find('\n', i);
            const std::string line = src.substr(i, (eol == std::string::npos ? src.size() : eol) - i);
            if (line.find("include") != std::string::npos &&
                line.find(name) != std::string::npos) {
                return true;
            }
            i = (eol == std::string::npos) ? src.size() : eol;
            continue;
        }
        if (c != ' ' && c != '\t' && c != '\r') blank = false;
        ++i;
    }
    return false;
}

} // namespace

std::string preludeFor(const std::string &src, int &lines) {
    lines = 0;
    if (!includesHeader(src, "iostream")) return std::string();
    // One line, so every diagnostic below it shifts by exactly one.
    lines = 1;
    return std::string(IOStreamNatives) + " " + IOStreamPrelude + "\n";
}
