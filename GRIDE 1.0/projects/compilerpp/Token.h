// Token.h -- the token set, shared by both layers.
//
// C++98 only.  No feature from C++11 or later is used anywhere in this project.

#ifndef TOKEN_H
#define TOKEN_H

#include <string>

enum TokenKind {
    TOK_EOF,
    TOK_IDENTIFIER,
    TOK_NUMBER,         // integer literal
    TOK_FLOATLIT,       // 1.5, 1e3, 1.5f
    TOK_CHARLIT,        // 'A'
    TOK_STRINGLIT,      // "text"

    // --- keywords the C layer needs ---
    TOK_INT,
    TOK_CHAR,
    TOK_VOID,
    TOK_BOOL,
    TOK_SHORT,
    TOK_LONG,
    TOK_SIGNED,
    TOK_UNSIGNED,
    TOK_FLOAT,
    TOK_DOUBLE,
    TOK_CONST,
    TOK_DO,
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_RETURN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_BREAK,
    TOK_CONTINUE,

    // --- keywords the C++ layer adds ---
    TOK_CLASS,
    TOK_STRUCT,
    TOK_PUBLIC,
    TOK_PRIVATE,
    TOK_PROTECTED,
    TOK_VIRTUAL,
    TOK_NEW,
    TOK_DELETE,
    TOK_THIS,
    TOK_TRUE,
    TOK_FALSE,

    // --- punctuation ---
    TOK_SEMI,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_COLON,      // :
    TOK_COLONCOLON, // ::
    TOK_DOT,        // .
    TOK_ARROW,      // ->
    TOK_TILDE,      // ~   (destructor names)
    // Recognised only so the parser can name the feature they belong to.
    TOK_ELLIPSIS,   // ...  variadic parameters
    TOK_HASH,       // #    a preprocessor directive

    // --- operators ---
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_ASSIGN,     // =
    TOK_PLUSPLUS,   // ++
    TOK_MINUSMINUS, // --
    TOK_PLUSEQ,     // +=
    TOK_MINUSEQ,    // -=
    TOK_STAREQ,     // *=
    TOK_SLASHEQ,    // /=
    TOK_PERCENTEQ,  // %=
    TOK_EQ,         // ==
    TOK_NE,         // !=
    TOK_LT,         // <
    TOK_GT,         // >
    TOK_LE,         // <=
    TOK_GE,         // >=
    TOK_ANDAND,     // &&
    TOK_OROR,       // ||
    TOK_NOT,        // !
    TOK_AMP,        // &
    TOK_SHL,        // <<
    TOK_SHR,        // >>

    // Operators of real C++ that this subset leaves out.  They are lexed for
    // the same reason TOK_RESERVED exists: without a token, `a ? 1 : 2` and
    // `a | b` reached the parser as an unknown character and were reported as
    // punctuation trouble, which reads as a broken compiler rather than as a
    // language that is smaller than expected.
    TOK_QUESTION,   // ?   the conditional operator
    TOK_PIPE,       // |   bitwise or
    TOK_CARET,      // ^   bitwise xor

    // A keyword of real C++ that this subset leaves out.  It is lexed rather
    // than left as an identifier so the parser can say WHICH feature is
    // missing, once, instead of failing its way through the construct.  The
    // set spans both layers -- goto and sizeof are C's, template and throw are
    // C++'s -- so it belongs to the shared lexer, not to either grammar.
    TOK_RESERVED,

    TOK_UNKNOWN
};

// The position is copied onto AST nodes as they are built, so the semantic
// pass can point at source just as a syntax error does.
struct Token {
    TokenKind kind;
    std::string text;       // identifier spelling, or a string literal's body
    long numberValue;       // integer and character literals
    double floatValue;      // floating literals
    bool isFloatSuffixed;   // 1.5f rather than 1.5
    int line;
    int col;
    Token()
        : kind(TOK_UNKNOWN), numberValue(0), floatValue(0.0),
          isFloatSuffixed(false), line(0), col(0) {}
};

// Human-readable spelling, for "expected X, found Y" messages.
const char *tokenName(TokenKind k);

// Why a reserved word is not available, and what to do instead.  Returns 0
// when the word is not one of them.
const char *reservedWordHelp(const std::string &word);

#endif
