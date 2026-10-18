// Spec: ISO C 7.19.6.2/9 and 7.20.1.3/3 - the input item of a floating conversion: the longest
// prefix of the input, within the width, that is or begins a floating constant (decimal, hex,
// INF, INFINITY, NAN, NAN(...)). One character is read past it and given back.

#include "Scanner.h"
#include "../ctype/CharacterClass.h"

namespace rts6x {

namespace {

bool hexDigit(int c) { return c >= 0 && CharacterClass::xdigit(c); }
bool decimalDigit(int c) { return c >= 0 && CharacterClass::digit(c); }

}  // namespace

int Scanner::floatText(int width, char *text)
{
    int left = width, n = 0;
    int c = in_.get();
    if (c == '+' || c == '-') { text[n++] = (char)c; c = advance(left); }
    int lower = c >= 0 ? CharacterClass::toLower(c) : c;
    if (lower == 'i' || lower == 'n') {
        const char *word = lower == 'i' ? "infinity" : "nan";
        for (int i = 0; word[i] && c >= 0 && CharacterClass::toLower(c) == word[i]; i++) {
            text[n++] = (char)c;
            c = advance(left);
            if (i == 2 && lower == 'i' && (c < 0 || CharacterClass::toLower(c) != 'i')) break;
        }
        if (lower == 'n' && n >= 3 && c == '(') {
            do { text[n++] = (char)c; c = advance(left); }
            while (c >= 0 && n < MaxText - 2 && (CharacterClass::alnum(c) || c == '_'));
            if (c == ')') { text[n++] = (char)c; c = advance(left); }
        }
        giveBack(c);
        return n;
    }
    bool hex = false;
    if (c == '0') {
        text[n++] = (char)c;
        c = advance(left);
        if (c == 'x' || c == 'X') { hex = true; text[n++] = (char)c; c = advance(left); }
    }
    bool point = false;
    for (; n < MaxText - 2; c = advance(left)) {
        if (c == '.' && !point) point = true;
        else if (!(hex ? hexDigit(c) : decimalDigit(c))) break;
        text[n++] = (char)c;
    }
    if (n > 0 && (hex ? (c == 'p' || c == 'P') : (c == 'e' || c == 'E'))) {
        text[n++] = (char)c;
        c = advance(left);
        if (c == '+' || c == '-') { text[n++] = (char)c; c = advance(left); }
        for (; decimalDigit(c) && n < MaxText - 2; c = advance(left)) text[n++] = (char)c;
    }
    giveBack(c);
    return n;
}

}  // namespace rts6x
