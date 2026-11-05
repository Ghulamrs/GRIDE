// Spec: a GRIDE extension (docs\Runtime_Design.md 14.4) - a double as the fewest significant digits that read back
// as the same value: nan, inf and -inf by name; plain notation from 1e-3 to below 1e16, a trailing ".0" on a
// whole number; otherwise d.ddde+XX. At most size - 1 characters and a NUL, as snprintf; the count the whole text
// had. Shalimar's runtime prints its reals with it (shmrt, shmrt6x); moved here from its Shortest.cpp.
#include <stdio.h>
#include <stdlib.h>
#include <cmath>

namespace {

int unpack(const char *buffer, char *digits, int &exponent)
{
    int count = 0;
    const char *p = buffer;
    if (*p == '-' || *p == '+') ++p;
    for (; *p && *p != 'e' && *p != 'E'; ++p) {
        if (*p >= '0' && *p <= '9') digits[count++] = *p;
    }
    while (count > 1 && digits[count - 1] == '0') --count;
    exponent = (*p ? atoi(p + 1) : 0) + 1;
    return count;
}

int shortestDigits(double v, char *digits, int &exponent)
{
    char buffer[64];
    for (int precision = 1; precision <= 17; ++precision) {
        snprintf(buffer, sizeof buffer, "%.*e", precision - 1, v);
        if (strtod(buffer, 0) == v) return unpack(buffer, digits, exponent);
    }
    snprintf(buffer, sizeof buffer, "%.17e", v);
    return unpack(buffer, digits, exponent);
}

}

extern "C" int _gride_shortest_double(char *out, size_t size, double v)
{
    if (std::isnan(v)) return snprintf(out, size, "nan");
    if (std::isinf(v)) return snprintf(out, size, v < 0 ? "-inf" : "inf");

    char digits[32];
    int exponent = 0;
    const int count = shortestDigits(v, digits, exponent);
    const bool negative = std::signbit(v);

    char body[400];
    size_t at = 0;
    if (exponent > 16 || exponent < -3) {
        body[at++] = digits[0];
        if (count > 1) {
            body[at++] = '.';
            for (int i = 1; i < count; ++i) body[at++] = digits[i];
        }
        const int e = exponent - 1;
        at += static_cast<size_t>(snprintf(body + at, sizeof body - at, "e%c%02d", e < 0 ? '-' : '+', e < 0 ? -e : e));
    } else if (exponent <= 0) {
        body[at++] = '0';
        body[at++] = '.';
        for (int i = 0; i < -exponent; ++i) body[at++] = '0';
        for (int i = 0; i < count; ++i) body[at++] = digits[i];
        body[at] = '\0';
    } else if (count <= exponent) {
        for (int i = 0; i < count; ++i) body[at++] = digits[i];
        for (int i = count; i < exponent; ++i) body[at++] = '0';
        body[at++] = '.';
        body[at++] = '0';
        body[at] = '\0';
    } else {
        for (int i = 0; i < exponent; ++i) body[at++] = digits[i];
        body[at++] = '.';
        for (int i = exponent; i < count; ++i) body[at++] = digits[i];
        body[at] = '\0';
    }
    return snprintf(out, size, "%s%s", negative ? "-" : "", body);
}
