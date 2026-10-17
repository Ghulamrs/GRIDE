// A program for the compiler in this project to compile and run.
//
// "Run project" builds compilerpp and then runs it, and compilerpp is itself
// a compiler: handed nothing it can only print its usage. So the project
// names this file and `-run` in its .pro, and Run compiles this with the
// compiler that was just built, in its own VM.
//
// It is written in the subset Compiler++ accepts: classes with virtual
// functions, references, arrays, and the iostream names without std::.
#include <iostream>

class Shape {
public:
    virtual int area() { return 0; }
    virtual ~Shape() {}
};

class Square : public Shape {
public:
    int side;
    Square(int s) { side = s; }
    virtual int area() { return side * side; }
};

class Rect : public Shape {
public:
    int w;
    int h;
    Rect(int a, int b) { w = a; h = b; }
    virtual int area() { return w * h; }
};

void grow(int &n) { n = n + 1; }

int main() {
    Square sq(4);
    Rect re(3, 5);
    Shape *shapes[2];
    shapes[0] = &sq;
    shapes[1] = &re;

    int total = 0;
    for (int i = 0; i < 2; i = i + 1) {
        cout << "area " << i << ": " << shapes[i]->area() << endl;
        total = total + shapes[i]->area();
    }
    cout << "total: " << total << endl;

    int n = 41;
    grow(n);
    cout << "grown: " << n << endl;
    return 0;
}
