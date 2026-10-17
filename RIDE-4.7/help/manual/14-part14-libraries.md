# Part XIV — The libraries that ship: `lib/` (C) and `include/` (C++)

The compilers are only as useful as the headers they can `#include`. This part is
what ships, where, and what is real versus simplified — so you know before you
write `#include` whether it will resolve, and to what.

--------------------------------------------------------------------------------
## 70. `lib/` — the C standard headers

`lib/` holds the C90 headers, searched by `c90` (its only system directory) and
by `cpp11` after `include/`. What is there:

    assert.h   ctype.h    errno.h    float.h    limits.h   locale.h
    math.h     setjmp.h   signal.h   stdarg.h   stddef.h   stdint.h
    stdio.h    stdlib.h   string.h   time.h     (and the platform's io/fcntl)

These are the real C90 interfaces: `printf`/`scanf` and the `FILE` I/O; `malloc`/
`free`/`qsort`/`bsearch`/`abs`/`atoi`/`strtol`; the `str*`/`mem*` family;
`<math.h>`'s `sin`/`cos`/`sqrt`/`pow`/`floor` and friends; `<ctype.h>`'s
character classification; `<setjmp.h>`; `<stdarg.h>`'s `va_*`; `<time.h>`. The
runtime behind them is the platform's C runtime (linked by the native linker),
except on tms6747, where the emulator supplies the runtime it needs (Part VI).

`<stddef.h>` is the file `cpp11` probes for when it decides whether a directory is
a real C header directory — which is why the Shalimar `bin\lib\` (only `.lib`/`.s`)
is never mistaken for the C `lib\`.

--------------------------------------------------------------------------------
## 71. `include/` — the C++ headers

`include/` holds the C++ headers, searched by `cpp11` first. Two kinds:

**The C headers wrapped into `std`** — `<cstddef>`, `<cstdlib>`, `<cstring>`,
`<cmath>`, `<cctype>` — each includes its `lib/` C header and pulls every name it
declares into namespace `std` with using-declarations. So `std::printf`,
`std::size_t`, `std::strlen`, `std::sqrt` work, and the underlying `<stdio.h>` etc.
are resolved in `lib/`.

**The C++ containers and utilities**, deliberately simplified but usable:

- **`<string>`** — a `string` class.
- **`<vector>`** — a growing array.
- **`<map>`** — a sorted vector of pairs.
- **`<set>`** — a sorted vector.
- **`<utility>`**, **`<algorithm>`** — the pieces the containers need.
- In all of them, **an iterator is a pointer**. This is the simplification to keep
  in mind: iterator arithmetic and range loops work because iterators are
  pointers, but the containers are not the full standard-library implementations —
  they are a working subset with the same shapes.

**The streams** — `<iostream>`, `<ostream>`, `<istream>`, `<sstream>`,
`<fstream>`, `<ios>`, `<cstdio>` — with `std::cout`, `std::cin`, `std::cerr` as
file-scope objects (aggregates with constant initialisers, whose `FILE*` is
resolved at the point of use, so no code runs before `main` to construct them).
`std::cout << x`, `std::cin >> v`, string streams and file streams work in the
ordinary way. `if (stream >> v)` — the conversion of a stream to a testable value —
works.

**`<new>`** and **`<typeinfo>`** — for placement `new`/`operator new` declarations
and for `typeid`/`type_info` (Part X).

--------------------------------------------------------------------------------
## 72. What is not in the library

The library is a working subset, not a conforming standard library. Do not expect:

- The full `<algorithm>` (only the pieces the containers use), the full iterator
  hierarchy (iterators are pointers), or the full `<functional>`/`<memory>`
  smart-pointer machinery.
- `<thread>`, `<mutex>`, `<atomic>`, `<chrono>`, `<regex>`, `<random>`,
  `<filesystem>` — the large C++11-and-later library components are not shipped.
- Locale/facet machinery beyond the C `<locale.h>`.
- Full exception-class hierarchies from `<stdexcept>` beyond what the shipped
  headers declare.

A program that includes a header not in `include/` fails at the include, by name —
the preprocessor cannot find the file — rather than miscompiling. When you need a
component that is not here, that is the boundary of the shipped library, not a bug.

--------------------------------------------------------------------------------
## 73. The Shalimar runtime as a "library"

Shalimar's equivalent of the C/C++ standard library is its **runtime**
(`shmrt-<target>` in `bin\lib\`): it provides `main`, the numeric formatting
behind `?`, the console I/O, the array operations and the trapping arithmetic. A
Shalimar program does not `#include` — it has no preprocessor — but it *borrows*
runtime functions with `uses`, and the runtime is what those names resolve to. On
tms6747 the runtime is the directory of `.s` the emulator assembles with the
program; on the host targets it is the `.lib`/`.a` the linker pulls in. The debug
runtime (`-debug`) is the same code plus a session that is dormant until the
environment arms it; what the compiler emits does not differ between the two by a
byte.
