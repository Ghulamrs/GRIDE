// sigjmp.cpp - the runtimes review's finding 13: a C++ program that longjmps out of a SIGSEGV handler went round for
// ever on both runtimes (the same C ran): cpp11 parked the comparison's operand before setjmp's call and read it back
// after longjmp's return. The operand that calls setjmp is evaluated first now, nothing parked.
#include <csetjmp>
#include <csignal>
#include <cstdio>

static std::jmp_buf there;

static void handler(int) { std::longjmp(there, 7); }

int main()
{
    int r;
    std::signal(SIGSEGV, handler);
    if ((r = setjmp(there)) == 0) {
        std::printf("signal: ");
        std::fflush(stdout);
        *(volatile int *)0 = 1;
        std::printf("not reached\n");
    } else {
        std::printf("SIGSEGV caught, longjmp'd out with %d\n", r);
    }
    std::printf("done\n");
    return 0;
}