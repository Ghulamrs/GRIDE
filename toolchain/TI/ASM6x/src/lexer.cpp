#include "asm.h"

#include <cctype>

std::string upper(const std::string &s)
{
    std::string r(s);
    for (size_t i = 0; i < r.size(); i++)
        r[i] = (char)toupper((unsigned char)r[i]);
    return r;
}

bool is_punct(const std::vector<Token> &t, size_t i, char c)
{
    return i < t.size() && t[i].kind == T_PUNCT && t[i].text.size() == 1 && t[i].text[0] == c;
}

static bool name_start(char c)
{
    return isalpha((unsigned char)c) || c == '_' || c == '$' || c == '.';
}

static bool name_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '$' || c == '.';
}

/* digits in a base, wrapped at 64 bits as asm6x wraps them (the directive that stores the
   value warns of the truncation) */
static bool digits_in(const std::string &d, int base, long long &v, std::string &err)
{
    unsigned long long acc = 0;
    for (size_t k = 0; k < d.size(); k++) {
        char c = (char)tolower((unsigned char)d[k]);
        int x = isdigit((unsigned char)c) ? c - '0' : c - 'a' + 10;
        if (x >= base) { err = "'" + d + "' is not a number in base " + std::to_string(base); return false; }
        acc = acc * (unsigned long long)base + (unsigned long long)x;
    }
    v = (long long)acc;
    return true;
}

/* a number: decimal, 0x hex, or TI's suffix forms 0FFh, 1010b and 17q */
static bool number(const std::string &s, size_t &i, long long &v, std::string &err)
{
    size_t j = i;
    if (s[j] == '0' && j + 1 < s.size() && (s[j + 1] == 'x' || s[j + 1] == 'X')) {
        j += 2;
        size_t start = j;
        while (j < s.size() && isxdigit((unsigned char)s[j])) j++;
        if (j == start) { err = "a hex number needs digits"; return false; }
        if (!digits_in(s.substr(start, j - start), 16, v, err)) return false;
        i = j;
        return true;
    }
    size_t start = j;
    while (j < s.size() && isxdigit((unsigned char)s[j])) j++;
    std::string digits = s.substr(start, j - start);
    if (j < s.size() && (s[j] == 'h' || s[j] == 'H') && !(j + 1 < s.size() && name_char(s[j + 1]))) {
        if (!digits_in(digits, 16, v, err)) return false;
        i = j + 1;
        return true;
    }
    if (j < s.size() && (s[j] == 'q' || s[j] == 'Q') && !(j + 1 < s.size() && name_char(s[j + 1]))) {
        if (!digits_in(digits, 8, v, err)) return false;
        i = j + 1;
        return true;
    }
    /* 1010b: the digits ran into the b already */
    if (digits.size() > 1 && (digits.back() == 'b' || digits.back() == 'B') && !(j < s.size() && name_char(s[j]))) {
        if (!digits_in(digits.substr(0, digits.size() - 1), 2, v, err)) return false;
        i = j;
        return true;
    }
    j = start;
    while (j < s.size() && isdigit((unsigned char)s[j])) j++;
    if (j == start) { err = "a number expected"; return false; }
    /* a leading zero is C's octal, as asm6x reads 017 */
    bool octal = j - start > 1 && s[start] == '0';
    if (!digits_in(s.substr(start, j - start), octal ? 8 : 10, v, err)) return false;
    i = j;
    return true;
}

/* one source line to tokens. TI's rules: `;` starts a comment, so does `*` in column 1; a
   name in column 1 is a label, with or without the colon - one is supplied, marked with
   value 1, so the parser sees one shape and knows it from a colon written elsewhere; `||`
   opens a parallel instruction; a string is raw, .cstring decodes the escapes itself */
bool split_line(const std::string &src, std::vector<Token> &out, std::string &err)
{
    out.clear();
    if (src.empty() || src[0] == ';' || src[0] == '*') return true;
    bool label = !isspace((unsigned char)src[0]) && name_start(src[0]);
    size_t i = 0;
    while (i < src.size()) {
        char c = src[i];
        if (c == ';') break;
        if (isspace((unsigned char)c)) { i++; continue; }
        Token t;
        t.value = 0;
        if (name_start(c)) {
            size_t j = i;
            while (j < src.size() && name_char(src[j])) j++;
            t.kind = T_NAME;
            t.text = src.substr(i, j - i);
            i = j;
            out.push_back(t);
            if (label && out.size() == 1) {
                Token colon; colon.kind = T_PUNCT; colon.text = ":"; colon.value = 1;
                if (i < src.size() && src[i] == ':') i++;
                out.push_back(colon);
            }
            continue;
        }
        if (isdigit((unsigned char)c)) {
            t.kind = T_NUM;
            size_t start = i;
            if (!number(src, i, t.value, err)) return false;
            t.text = src.substr(start, i - start);
            out.push_back(t);
            continue;
        }
        if (c == '"') {
            /* raw, as asm6x reads a string: a backslash is a byte; .cstring decodes escapes itself */
            size_t j = i + 1;
            while (j < src.size() && src[j] != '"') j++;
            if (j >= src.size()) { err = "an unclosed string"; return false; }
            t.kind = T_STR;
            t.text = src.substr(i + 1, j - i - 1);
            out.push_back(t);
            i = j + 1;
            continue;
        }
        if (c == '\'') {
            /* 'a', and '''' for the quote itself; asm6x knows no escapes here - '\n' is the
               two characters, packed low byte first, and a .byte of it warns and keeps the \ */
            if (src.compare(i, 4, "''''") == 0) {
                t.kind = T_NUM; t.value = '\''; t.text = "''''"; out.push_back(t); i += 4; continue;
            }
            size_t j = i + 1;
            while (j < src.size() && src[j] != '\'') j++;
            if (j >= src.size() || j == i + 1 || j - i - 1 > 4) { err = "a character constant is one to four characters in quotes"; return false; }
            t.kind = T_NUM; t.value = 0;
            for (size_t k = i + 1; k < j; k++) t.value |= (long long)(unsigned char)src[k] << (8 * (k - i - 1));
            t.text = src.substr(i, j - i + 1);
            out.push_back(t);
            i = j + 1;
            continue;
        }
        if ((c == '<' || c == '>') && i + 1 < src.size() && src[i + 1] == c) {
            t.kind = T_PUNCT; t.text = std::string(2, c); out.push_back(t); i += 2; continue;
        }
        if (c == '|' && i + 1 < src.size() && src[i + 1] == '|') {
            t.kind = T_PUNCT; t.text = "||"; out.push_back(t); i += 2; continue;
        }
        if (c == '+' && i + 1 < src.size() && src[i + 1] == '+') {
            t.kind = T_PUNCT; t.text = "++"; out.push_back(t); i += 2; continue;
        }
        if (c == '-' && i + 1 < src.size() && src[i + 1] == '-') {
            t.kind = T_PUNCT; t.text = "--"; out.push_back(t); i += 2; continue;
        }
        t.kind = T_PUNCT;
        t.text = std::string(1, c);
        out.push_back(t);
        i++;
    }
    return true;
}
