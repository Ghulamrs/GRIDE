// shapes.cpp - C++ on the TMS320C6747 (example 2): classes with virtual functions, a template and
// an exception, through cpp11, asm6x and lnk6x against the RTS6x runtime, run by sim6747.

#include <cstdio>

class Shape {
public:
    virtual ~Shape() {}
    virtual const char *name() const = 0;
    virtual int area() const = 0;
};

class Rect : public Shape {
public:
    Rect(int w, int h) : w_(w), h_(h) {}
    const char *name() const { return "rect"; }
    int area() const { return w_ * h_; }
private:
    int w_;
    int h_;
};

class Square : public Rect {
public:
    explicit Square(int side) : Rect(side, side) {}
    const char *name() const { return "square"; }
};

class Triangle : public Shape {
public:
    Triangle(int base, int height) : b_(base), h_(height) {}
    const char *name() const { return "triangle"; }
    int area() const { return b_ * h_ / 2; }
private:
    int b_;
    int h_;
};

template <typename T, int N>
T largest(const T (&v)[N])
{
    T best = v[0];
    for (int i = 1; i < N; ++i)
        if (v[i] > best) best = v[i];
    return best;
}

struct TooBig {
    explicit TooBig(int a) : area(a) {}
    int area;
};

static void check(const Shape &s, int limit)
{
    if (s.area() > limit) throw TooBig(s.area());
}

int main()
{
    Rect r(3, 4);
    Square q(5);
    Triangle t(6, 7);
    const Shape *shapes[] = { &r, &q, &t };
    int areas[3];

    for (int i = 0; i < 3; ++i) {
        areas[i] = shapes[i]->area();
        std::printf("%s area %d\n", shapes[i]->name(), areas[i]);
    }
    std::printf("largest area %d\n", largest(areas));
    try {
        for (int i = 0; i < 3; ++i) check(*shapes[i], 24);
        std::printf("no shape is too big\n");
    } catch (const TooBig &e) {
        std::printf("caught TooBig: area %d\n", e.area);
    }
    return 0;
}
