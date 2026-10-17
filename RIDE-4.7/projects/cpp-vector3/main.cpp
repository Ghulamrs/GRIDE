// main.cpp - two vectors, their dot and cross products, and how long each is.
#include <cstdio>
#include "vector3.h"

static void show(const char* name, const Vector3& v) {
    std::printf("%-7s (%g, %g, %g)  length %.4f\n", name, v.x, v.y, v.z, length(v));
}

int main() {
    Vector3 x(1, 0, 0), y(0, 1, 0);
    Vector3 a(1, 2, 3), b(4, 5, 6);

    show("x", x);
    show("x * y", cross(x, y));          // right-handed: x cross y is z
    show("a", a);
    show("b", b);
    show("a * b", cross(a, b));
    std::printf("a . b   %g\n", dot(a, b));
    std::printf("(a * b) . a = %g, so the cross product is at right angles to a\n", dot(cross(a, b), a));
    return 0;
}
