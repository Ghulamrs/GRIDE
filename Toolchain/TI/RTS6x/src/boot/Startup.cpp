// Spec: SPRAB89B 18.3 - each cinit record is source, destination; the first source byte indexes
// the handler table, and the handler is given the bytes after it and the destination. ISO C
// 5.1.2.2.1/2: argc non-negative, argv[argc] a null pointer. C++ 3.6.2: .init_array in order.

#include "Startup.h"
#include "../exit/Termination.h"

namespace rts6x {

void Startup::initialiseData()
{
    for (const unsigned char *r = __rts6x_bounds[CinitBase]; r < __rts6x_bounds[CinitLimit]; r += 8) {
        const unsigned char *source = reinterpret_cast<const unsigned char *>(load32(r));
        unsigned char *destination = reinterpret_cast<unsigned char *>(load32(r + 4));
        const unsigned char *slot = __rts6x_bounds[HandlerTable] + 4 * source[0];
        Handler h = reinterpret_cast<Handler>(load32(slot));
        h(source + 1, destination);
    }
}

void Startup::construct()
{
    for (const unsigned char *p = __rts6x_bounds[InitArrayBase]; p < __rts6x_bounds[InitArrayLimit]; p += 4)
        reinterpret_cast<Constructor>(load32(p))();
}

int Startup::arguments(char **&vector)
{
    static char *none[1] = { 0 };
    if (__c_args__ == reinterpret_cast<char *>(-1)) { vector = none; return 0; }
    // .args: argc, then argv's pointers, the last of them null.
    int count = (int)load32(reinterpret_cast<const unsigned char *>(__c_args__));
    vector = reinterpret_cast<char **>(__c_args__ + 4);
    return count;
}

void Startup::run()
{
    initialiseData();
    construct();
    char **argv;
    int argc = arguments(argv);
    Termination::exit(main(argc, argv));
}

}  // namespace rts6x

extern "C" void __rts6x_start(void)
{
    rts6x::Startup::run();
}
