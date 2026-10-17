// shape.h - what every shape can say about itself.
#ifndef SHAPE_H
#define SHAPE_H

class Shape {
public:
    virtual ~Shape() {}
    virtual const char *kind() const = 0;
    virtual double area() const = 0;
    virtual double perimeter() const = 0;
};

Shape *make_circle(double r);
Shape *make_rect(double w, double h);

#endif
