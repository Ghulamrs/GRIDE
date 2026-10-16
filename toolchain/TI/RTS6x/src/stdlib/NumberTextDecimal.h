// Spec: ISO C 7.20.1.2 - atoi and atol: strtol (7.20.1.4) in base 10 with no end pointer. Not a
// header of declarations: the body of both, included as each one's braces, since a call to a
// shared reader costs cpp11 about 25 cycles - a sixth of reading a five-digit number.

{
    // The including file brings <errno.h>, <limits.h> and ErrorNumber.h, ahead of its declarator.
    // Few locals on purpose: cpp11 keeps about eight in registers, and a spill costs a load.
    const unsigned char *s = (const unsigned char *)text;
    unsigned c = *s;
    while (c <= ' ' && ((c == ' ') | (c - '\t' <= '\r' - '\t'))) c = *++s;
    unsigned negative = c - '+';
    if ((negative | 2) == 2) {
        // '+' or '-', which are two apart.
        negative >>= 1;
        c = *++s;
    } else {
        negative = 0;
    }
    unsigned d = c - '0';
    if (d > 9) return 0;
    const unsigned char *first = s;
    unsigned long v = 0;
    do {
        c = s[1];
        s++;
        v = (v << 3) + (v << 1) + d;
        d = c - '0';
    } while (d <= 9);
    if (s - first > 9) {
        // Ten significant digits below 3 000 000 000 are below 2^32, so v is exact; beyond that,
        // or past ten digits, the value is past LONG_MAX (where C leaves atoi undefined: clamped).
        while (*first == '0') first++;
        long n = s - first;
        unsigned past = (n > 10) | ((n == 10) & ((*first > '2') | (v > (unsigned long)LONG_MAX + negative)));
        if (past) {
            rts6x::ErrorNumber::set(ERANGE);
            return negative ? LONG_MIN : LONG_MAX;
        }
    }
    return (long)((v ^ (0ul - negative)) + negative);  // v, or its negation where a '-' was read
}
