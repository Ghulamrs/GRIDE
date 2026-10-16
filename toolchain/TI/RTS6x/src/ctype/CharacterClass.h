// Spec: ISO C 7.4 and 7.4.1 in the "C" locale (7.4/1 and 5.2.1): the classes of a character's
// value as an unsigned char, EOF in none of them; 7.4.2 the case mappings. ASCII, by range.
#ifndef RTS6X_CHARACTER_CLASS_H
#define RTS6X_CHARACTER_CLASS_H

namespace rts6x {

class CharacterClass {
public:
    static bool upper(int c) { return c >= 'A' && c <= 'Z'; }
    static bool lower(int c) { return c >= 'a' && c <= 'z'; }
    static bool alpha(int c) { return upper(c) || lower(c); }
    static bool digit(int c) { return c >= '0' && c <= '9'; }
    static bool alnum(int c) { return alpha(c) || digit(c); }
    static bool xdigit(int c) { return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
    static bool space(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
    static bool control(int c) { return (c >= 0 && c < 0x20) || c == 0x7F; }
    static bool print(int c) { return c >= 0x20 && c < 0x7F; }
    static bool graph(int c) { return c > 0x20 && c < 0x7F; }
    static bool punct(int c) { return graph(c) && !alnum(c); }
    static int toLower(int c) { return upper(c) ? c - 'A' + 'a' : c; }
    static int toUpper(int c) { return lower(c) ? c - 'a' + 'A' : c; }
};

}  // namespace rts6x

#endif
