# Part IX — Worked examples

Every example here is a complete program and the exact commands to build and run
it, with what comes out. They are ordered from the smallest to the whole
lifecycle, and they cover all three languages, the four targets, projects, the
converter and the TI build. Run them from the install's `bin\` on `PATH`.

--------------------------------------------------------------------------------
## 37. C, one file

**hello.c**

    #include <stdio.h>
    int main(void)
    {
        printf("hello\n");
        return 0;
    }

Build and run on the host:

    c90 hello.c -o hello
    ./hello                  →  hello

Just the assembly, and read it:

    c90 -S hello.c -o hello.s

The compiler announces itself (`©2026 G. R. Akhtar - ISO C 90`) unless you pass
`-nologo`. The program's return value is what the editor shows as
`[program returned 0]`.

--------------------------------------------------------------------------------
## 38. C, several files that link

**sum.h**

    int add_up(int a, int b);

**sum.c**

    #include "sum.h"
    int add_up(int a, int b) { return a + b; }

**main.c**

    #include <stdio.h>
    #include "sum.h"
    int main(void)
    {
        printf("answer %d\n", add_up(2, 40));
        return 0;
    }

    c90 main.c sum.c -o prog
    ./prog                   →  answer 42

Several inputs on one command line link into one program. Headers are found next
to the file that includes them and by any `-I` directory.

--------------------------------------------------------------------------------
## 39. C++, classes and virtual dispatch

**shape.cpp**

    #include <cstdio>
    struct Shape { virtual int area() const = 0; virtual ~Shape() {} };
    struct Square : Shape { int s; Square(int x) : s(x) {} int area() const { return s * s; } };
    int main()
    {
        Shape* p = new Square(6);
        std::printf("area=%d\n", p->area());
        delete p;
        return 0;
    }

    cpp11 shape.cpp -o shape
    ./shape                  →  area=36

This exercises an abstract base, a pure-virtual override, virtual dispatch
through the vtable, `new`/`delete` and a virtual destructor — all supported.

--------------------------------------------------------------------------------
## 40. C++, operator new/delete (global, class, placement)

    #include <cstdio>
    #include <new>
    #include <cstdlib>
    static int g = 0, c = 0;
    void* operator new(std::size_t n) { ++g; return std::malloc(n); }
    void  operator delete(void* p) noexcept { std::free(p); }
    struct Counter {
        int v; Counter(int x) : v(x) {}
        static void* operator new(std::size_t n) { ++c; return std::malloc(n); }
        static void  operator delete(void* p) noexcept { std::free(p); }
    };
    int main() {
        int* a = new int(5);
        Counter* k = new Counter(9);
        char buf[sizeof(Counter)];
        Counter* p = new (buf) Counter(42);   /* placement: no allocation */
        std::printf("%d %d %d  g=%d c=%d\n", *a, k->v, p->v, g, c);
        delete a; delete k;
        return 0;
    }

    cpp11 newdel.cpp -o newdel
    ./newdel                 →  5 9 42  g=1 c=1

The global `operator new` fires for `int`, the class one for `Counter`,
placement allocates nothing, and each `delete` routes to the matching operator.

--------------------------------------------------------------------------------
## 41. Shalimar, one file

**gcd.shl**

    fun <> = main() {
      a : 48
      b : 18
      while b != 0 {
        r : a % b
        a : b
        b : r
      }
      ? "gcd" a
    }

    shalimar gcd.shl -o gcd
    ./gcd                    →  gcd 6

Assignment is `name : value`; `?` prints (a space after each item); `while`
controls flow. `shalimar` links the Shalimar runtime from `bin\lib\` for you.

--------------------------------------------------------------------------------
## 42. The same programs on the four targets

Assembly for any target, on any host:

    c90 -S -arch x86_64-linux   hello.c -o hello-linux.s
    c90 -S -arch arm64-darwin   hello.c -o hello-mac.s
    c90 -S -arch x86_64-windows hello.c -o hello-win.asm     (MASM spelling)
    c90 -S -arch x86_64-windows -masm=gnu hello.c -o hello-win-gnu.s

Run on the host target (build a program):

    c90 -arch x86_64-windows hello.c -o hello.exe     (on the Windows box)

Run on the C6000 through the emulator (any host):

    c90 -S -arch tms6747 hello.c -o hello.s
    vm6747 hello.s          →  hello   [program returned 0]

A foreign target from the editor: pressing F5 on `arm64-darwin` on the Windows
box shows `x86_64-linux only reaches -S here — switch to x86_64-windows to run
it` for the wrong host, and the Assembly tab shows the generated code.

--------------------------------------------------------------------------------
## 43. A mixed C-and-C++ project

A project whose `Sources` group holds both languages builds each with its own
compiler and links the objects. `four.pro`:

    {
      "name": "four", "toolchain": "auto", "arch": "x86_64-windows",
      "groups": { "Sources": ["main.c", "sum.c", "shape.cpp"] },
      "build": { "target": "four", "groups": ["Sources"] }
    }

with `main.c` calling both a C function (`add_up`) and a C++ one (`area`). In the
editor, F4 builds it; the console shows two `Assembling` lines from `c90` (the C
part), then `cpp11` compiling `shape.cpp`, then the link, and Run project prints
`answer 42, area 12`. By hand it is the same steps: `c90 -c main.c sum.c`,
`cpp11 -c shape.cpp`, then link the three objects with the C++ driver.

--------------------------------------------------------------------------------
## 44. The whole project lifecycle in the editor

1. **Project ▸ New…** — pick a folder, name it `demo`; `demo.pro` is written.
2. **Project ▸ New File** (Ctrl-N) — `greet.c`; it is created empty and added.
3. Type into it: `int greet(void) { return 7; }`; save.
4. **New File** `main.c`: `#include <stdio.h>` / `int greet(void);` /
   `int main(void){ printf("greet is %d\n", greet()); return 0; }`. The `.pro`
   now lists both under `Sources`.
5. **F4** builds `demo`; **Run project** prints `greet is 7`.
6. **Open** an existing `extra.c` (`int extra(void){return 35;}`),
   **Project ▸ Add File** → `Sources`; change `main.c` to call `extra()`; Run
   project → `greet 42, extra 35`. The `.pro`'s `Sources` now lists `extra.c`.
7. **Project ▸ Remove File** on `extra.c` (it leaves the file on disk); change
   `main.c` back; Run project → `greet is 7`. The `.pro` no longer lists it.

Every add and remove is reflected in the `.pro` immediately. This is exactly the
`projects/c-demo` project shipped with the install.

--------------------------------------------------------------------------------
## 45. Converting between C and Shalimar

C to Shalimar, and compile the result:

    c2s primes.c            →  primes.shl
    shalimar primes.shl -o primes && ./primes

Shalimar to C, and compile the result with the host compiler:

    c2s primes.shl          →  primes.c (with the c2s_* preamble)
    c90 primes.c -o primes2 && ./primes2

Ask for the line map and the pragmatic rewrites:

    c2s --lines --pragmatic app.c

When `c2s` refuses a construct it names it with a line (code C2100) — e.g. a
`%4d` width, a `?:` inside a larger expression, or a `long`. The message says
what to write instead.

--------------------------------------------------------------------------------
## 46. A real C674x binary (.out / .hex)

The emulator runs the program; the TI build path makes a real chip binary:

    bin\ti\ti-build.cmd  fib.c

produces `fib.out` (a C674x ELF: `e_machine` 140, 32-bit, little-endian, EXEC)
and `fib.hex` (Intel hex). It finds a TI CGT on the machine, builds the EH
runtime once into `%LOCALAPPDATA%\RIDE\tilib`, then drives
`cl6x`/`asm6x`/`lnk6x`/`hex6x`. The same works for `.cpp` and `.shl`. To run and
test the program instead, the emulator needs no TI install:

    c90 -S -arch tms6747 fib.c -o fib.s && vm6747 fib.s
