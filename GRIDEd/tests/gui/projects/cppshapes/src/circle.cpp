// circle.cpp - a circle, known to the rest only through Shape.
#include "shape.h"

namespace {
const double pi = 3.14159265358979;

class Circle : public Shape {
public:
    explicit Circle(double r) : r_(r) {}
    const char *kind() const { return "circle"; }
    double area() const { return pi * r_ * r_; }
    double perimeter() const { return 2 * pi * r_; }
private:
    double r_;
};
}

Shape *make_circle(double r) { return new Circle(r); }
