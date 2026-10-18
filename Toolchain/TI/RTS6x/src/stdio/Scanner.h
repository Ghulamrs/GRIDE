// Spec: ISO C 7.19.6.2 (fscanf) - the directives of the scanf family: white space, ordinary
// characters, and conversions with assignment suppression, a field width and a length modifier.
// One class for fscanf, scanf and sscanf; each conversion a member, in a source file of its own.
#ifndef RTS6X_SCANNER_H
#define RTS6X_SCANNER_H

#include <stdarg.h>
#include "InputSource.h"

namespace rts6x {

class Scanner {
public:
    Scanner(InputSource &in, va_list *args) : in_(in), args_(args), assigned_(0), converted_(false) {}
    // The number of items assigned, or EOF for an input failure before the first conversion.
    int run(const char *format);

private:
    // The length modifiers, as they decide the type an argument points at.
    enum Length { Default, Char, Short, Long, LongLong, LongDouble, Size };
    enum { MaxText = 512, Unread = -2 };

    // Scanner.cpp
    void skipSpace();
    // One character of a field used: the next one, or Unread once the width is spent.
    int advance(int &left) { return --left > 0 ? in_.get() : (int)Unread; }
    // The character after a field given back, unless the width stopped the reading first.
    void giveBack(int c) { if (c != Unread) in_.unget(c); }
    // A conversion; false on a matching or input failure. input is set for the second.
    bool convert(char conversion, bool assign, int width, Length length, const char *&format, bool &input);

    // ScanNumber.cpp: d i o u x X p, and a e f g
    bool integer(char conversion, bool assign, int width, Length length, bool &input);
    bool floating(bool assign, int width, Length length, bool &input);
    // The longest prefix of a floating constant within width, into text; its length.
    int floatText(int width, char *text);

    // ScanText.cpp: c s [
    bool characters(bool assign, int width, Length length, bool &input);
    bool word(bool assign, int width, Length length, bool &input);
    bool scanset(bool assign, int width, Length length, const char *&format, bool &input);
    // Stores c at position i of the argument: a char, or a wide one for the l modifier.
    static void store(void *target, int i, int c, Length length);

    InputSource &in_;
    va_list *args_;
    int assigned_;
    bool converted_;
};

}  // namespace rts6x

#endif
