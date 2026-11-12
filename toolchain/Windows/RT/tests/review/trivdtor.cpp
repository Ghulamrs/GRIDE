// trivdtor.cpp - the runtimes review's finding 10: a static object whose constructor is trivial and whose destructor
// is not was never destroyed, on either target - cpp11 registered nothing for an object with no initialiser. Now
// each is destroyed: at namespace scope, as a function's static, beside one with a constructor of its own.
#include <cstdio>

struct Trivial {
    const char *name;
    ~Trivial() { std::printf("%s destroyed\n", name ? name : "a trivially made object"); }
};
struct Made {
    Made() {}
    ~Made() { std::printf("the object with a constructor destroyed\n"); }
};

Trivial first;
Made second;
static Trivial third;

static void local()
{
    static Trivial inside;
    std::printf("local's static is there\n");
}

int main()
{
    local();
    std::printf("main\n");
    return 0;
}