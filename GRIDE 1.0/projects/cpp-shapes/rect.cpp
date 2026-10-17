// rect.cpp - a rectangle, the same way.
#include "shape.h"

namespace {
class Rect : public Shape {
public:
    Rect(double w, double h) : w_(w), h_(h) {}
    const char *kind() const { return "rect"; }
    double area() const { return w_ * h_; }
    double perimeter() const { return 2 * (w_ + h_); }
private:
    double w_, h_;
};
}

Shape *make_rect(double w, double h) { return new Rect(w, h); }
