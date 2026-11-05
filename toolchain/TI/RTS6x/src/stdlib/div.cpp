// Spec: ISO C 7.20.6.2 - div: the quotient and the remainder of one division. The structure is spelt here and not
// <stdlib.h>'s: there, in C++, div is a set of overloads, and this is C's function.

struct div_answer { int quot; int rem; };

extern "C" div_answer div(int numerator, int denominator)
{
    div_answer r;
    r.quot = numerator / denominator;
    r.rem = numerator % denominator;
    return r;
}
