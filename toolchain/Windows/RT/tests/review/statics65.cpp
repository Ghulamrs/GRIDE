// statics65.cpp - the runtimes review's finding 2 on the C6747 (its atexit6e.cpp): 65 namespace-scope objects with
// destructors and then an atexit function. The 65th object and the function were refused there and nothing said
// so; every one is destroyed now, the function first, the first object made last.
#include <cstdio>
#include <cstdlib>

static int made, destroyed;

struct Counted {
    int id;
    Counted() : id(++made) {}
    ~Counted() {
        ++destroyed;
        if (id == 1) std::printf("%d of %d destroyed, the first made last\n", destroyed, made);
    }
};

#define FIVE(n) static Counted n##1, n##2, n##3, n##4, n##5;
FIVE(a) FIVE(b) FIVE(c) FIVE(d) FIVE(e) FIVE(f) FIVE(g) FIVE(h) FIVE(i) FIVE(j) FIVE(k) FIVE(l) FIVE(m)

static void atexitFunction() { std::printf("the atexit function ran, %d destroyed before it\n", destroyed); }

int main()
{
    int answered = std::atexit(atexitFunction);
    std::printf("%d objects made, atexit answered %d\n", made, answered);
    return 0;
}