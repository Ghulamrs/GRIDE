// Spec: SPRAB89B 18.3 (the .cinit table, __TI_CINIT_Base and _Limit, the handler table) and
// ISO C 5.1.2.2 (main called with argc and argv, its return given to exit); C++ 3.6.2 (the
// constructors of .init_array run before main). One class does all of it, once, at _c_int00.
#ifndef RTS6X_STARTUP_H
#define RTS6X_STARTUP_H

extern "C" {
// lnk6x's table bounds, through boot.s's weak references (0 where the image has no such table):
// .cinit's records, the handler table, .init_array's constructors. And the arguments - __c_args__,
// the absolute address -1 when the link gave none (--args=N lays them out).
extern const unsigned char *const __rts6x_bounds[5];
extern char __c_args__[];
int main(int argc, char **argv);
void __rts6x_start(void);
}

namespace rts6x {

class Startup {
public:
    // From _c_int00, once, and never returns: the data, the constructors, main, exit.
    static void run();

private:
    // The order of boot.s's __rts6x_bounds.
    enum Bound { CinitBase, CinitLimit, HandlerTable, InitArrayBase, InitArrayLimit };
    typedef void (*Handler)(const unsigned char *source, unsigned char *destination);
    typedef void (*Constructor)(void);

    static void initialiseData();
    static void construct();
    // argc, and argv in `vector`: .args when the link laid it out, else none at all.
    static int arguments(char **&vector);
    static unsigned load32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
};

}  // namespace rts6x

#endif
