// main.cpp - three shapes behind one pointer type, the largest one found.
#include <cstdio>
#include <vector>
#include "shape.h"

int main() {
    std::vector<Shape *> shapes;
    shapes.push_back(make_circle(1.5));
    shapes.push_back(make_rect(3, 4));
    shapes.push_back(make_rect(2.5, 2.5));

    const Shape *largest = 0;
    for (std::size_t i = 0; i < shapes.size(); i++) {
        const Shape *s = shapes[i];
        std::printf("%-6s area %6.2f  perimeter %6.2f\n", s->kind(), s->area(), s->perimeter());
        if (largest == 0 || s->area() > largest->area()) largest = s;
    }
    std::printf("largest: %s\n", largest->kind());
    for (std::size_t i = 0; i < shapes.size(); i++) delete shapes[i];
    return 0;
}
