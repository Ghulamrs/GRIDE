# Part II — The three languages: what each supports, and what it does not

RIDE compiles three languages with three compilers. This part says, for each,
what it accepts and — with the same care — what it refuses. The refusals matter
as much as the features: each compiler here **refuses by name and never accepts
quietly**, so a construct it does not support stops the build with a message
rather than miscompiling.

--------------------------------------------------------------------------------
## 7. C — `c90`

**Standard: ISO C 90 (ANSI C / C89).** `c90` is an ANSI C compiler. It is the
oldest and most complete of the three; C90 is a small, closed language and
`c90` implements it.

**What it supports.** The whole of C90: the type system (integer and floating
types, `struct`, `union`, `enum`, bit-fields, pointers, arrays, function
pointers, `const`/`volatile` qualifiers), the full expression grammar with C's
integer promotions and usual arithmetic conversions, all statements and control
flow including `switch` with fall-through and `goto` with labels, the storage
classes (`auto`, `register`, `static`, `extern`, `typedef`), a complete
preprocessor (`#define` object- and function-like macros, `#include`, the full
`#if`/`#ifdef`/`#elif`/`#else`/`#endif` family, `#line`, `#error`, `#pragma`,
stringising `#` and pasting `##`), and the standard library headers shipped in
`lib/` (`stdio.h`, `stdlib.h`, `string.h`, `math.h`, `ctype.h`, `assert.h`,
`errno.h`, `limits.h`, `float.h`, `stddef.h`, `stdarg.h`, `time.h`, `setjmp.h`,
`signal.h`, `locale.h`, and the rest). Variadic functions work — both calling
them and defining them with `va_start`/`va_arg`/`va_end`. A character constant
is an `int`, as C90 says.

**What it does not do.** `c90` is C90, not C99 or C11, so the later additions
are not there: no `//` line comments as a guaranteed dialect feature beyond what
the lexer accepts, no `long long` as a *standard* type name expectation, no
mixed declarations-and-code required by C99, no variable-length arrays, no
`inline` as a C99 keyword, no `_Bool`/`<stdbool.h>`, no `restrict`, no designated
initialisers, no compound literals, no `_Complex`, no `<stdint.h>` guarantees
beyond what `lib/` provides. It does not compile C++ — hand it a `.cpp` and it
refuses by name and points at `cpp11`. It is not a linker or an assembler; it
emits assembly and calls the host tools (Part I chapter 5).

**Targets.** All four: `x86_64-linux`, `x86_64-windows`, `arm64-darwin`,
`tms6747`. Debug information: DWARF on the two GNU targets, CodeView on
`x86_64-windows` in the GNU spelling a Debug build uses (none in the MASM
spelling, Part V), and none on `tms6747` (the emulator and the simulator run
it, no debugger reads it).

--------------------------------------------------------------------------------
## 8. C++ — `cpp11`

**Standard: ISO C++ 11, minus a documented list of exclusions.** That headline
needs the second half to be honest: `cpp11` accepts *C++11 minus a list*, and
ships the simplified library in `include/` rather than a fully conforming one. A
C++11 compiler that refuses a feature is a C++11 *subset*, and this manual says
which subset so a program that fails has somewhere to look. The authoritative,
source-derived inventory is `docs/EXCLUSIONS.md` in the C++ compiler's own tree
(`tools/exclusions` regenerates it); what follows is the shape of it.

**What it supports — the large majority of C++11:**

- **Classes** with data and function members, access control, constructors
  (default, parameterised, copy, and move where the shapes are simple),
  destructors, `this`, member initialiser lists, nested classes, `friend`.
- **Inheritance**, single and multiple; **virtual functions** and dynamic
  dispatch through a vtable; pure-virtual functions and abstract classes;
  **virtual (shared) base classes** — full Itanium virtual inheritance on the
  three Itanium targets (secondary vtables, the VTT, `_ZTv` virtual thunks,
  construction vtables, the C2/D2 constructors that take a VTT), matching
  clang's tables. (Virtual inheritance was `sizeof`-only earlier; it is real
  now.)
- **Templates**: class and function templates, explicit specialisation
  (`template <> struct Box<int> {…}`), partial specialisation, non-type
  parameters, template instantiation and two-phase name resolution to the
  extent C++11 requires.
- **Exceptions**: `throw`/`try`/`catch`, stack unwinding running destructors,
  catch-by-value (copying from `__cxa_begin_catch`), catch-by-reference, a
  thrown pointer, rethrow, `noexcept`/`throw()` with `std::terminate` on an
  escaping exception, and a destructor that throws while unwinding terminating —
  on the three Itanium targets, with the target's own exception tables.
- **RTTI**: `typeid`, static and dynamic; `type_info`; `dynamic_cast` to a
  pointer (including `dynamic_cast<void*>`, the most-derived form) along a
  single-base chain.
- **`operator new`/`operator delete`**: the replaceable global forms, class-
  specific forms, and placement new — each dispatching to the right operator.
- **Lambdas**, `auto` for variables, `nullptr`, `enum class` and an enum's
  underlying type, `alignas`/`alignof`, `static_assert`, range-based ideas to
  the extent the library supports them, references and rvalue references where
  the shapes are simple, member and function pointers including pointer-to-
  virtual-member dispatch, and `constexpr` in the forms the compiler implements.
- **A working, if simplified, library** in `include/`: `<cstddef>`, `<cstdlib>`,
  `<cstring>`, `<cmath>`, `<cctype>` (each the C header wrapped into `std`);
  `<string>` as a class; `<utility>`, `<vector>`, `<map>`, `<set>`, `<algorithm>`
  (a vector is a growing array, a map a sorted vector of pairs, a set a sorted
  vector, and an iterator in each is a pointer); and the streams —
  `<iostream>`, `<ostream>`, `<istream>`, `<sstream>`, `<fstream>`, `<ios>`,
  `<cstdio>`, with `std::cout`/`cin`/`cerr` as file-scope objects.

**What it refuses — the exclusions (each refused by name):**

- **`dynamic_cast` to a reference** — it has no null to return, so it is refused
  rather than half-implemented; use the pointer form and test for null.
- **`dynamic_cast` naming a class with more than one base** — that needs
  `__vmi_class_type_info`, which is not emitted; refused.
- **A member *class* template** — refused (a member *function* template is
  fine).
- **A trailing return type** `auto f(int) -> int` — refused (documented
  exclusion).
- **Objects that would run code before `main`** in the forms the compiler does
  not do dynamic initialisation for — refused by name.
- **C++14 and C++17 forms** — refused *naming the standard version*, because the
  target is C++11 (e.g. C++14 `constexpr` relaxations, C++14/17-only syntax).
- **Eight keywords the parser has no rule for yet** — refused by name as "does
  not begin an expression" / "not supported yet" (see `pending[]` in the
  parser).
- **`volatile`** — accepted in the grammar but **read and dropped**: it does not
  change code generation. Do not rely on it for memory-mapped I/O.
- **A few target-only refusals** — e.g. a polymorphic virtual base is refused by
  name on `x86_64-windows` (Microsoft ABI), while the Itanium targets accept it.

The rule to work by: **if `cpp11` refuses something, it will say so by name.** A
program that compiles did not step on an exclusion; a program that fails names
the reason. When in doubt about a single construct, compile a one-line program —
some refusals read more broadly than they actually fire.

--------------------------------------------------------------------------------
## 9. Shalimar — `shalimar`

Shalimar is a small numeric language. The full, authoritative reference is
Appendix A of this help set (`help/appendix-a-shalimar-language.md`, ~1,400
lines, a verbatim copy of the language's own specification), and the compiler's
own deviations from the interpreter are in the C++... in the Shalimar compiler's
`docs/CONFORMANCE.md`. This chapter is the shape and the boundaries.

**What Shalimar is.** A program is a set of functions; execution begins at
`main`. The syntax is deliberately spare: a function is
`fun <capture> = name(params) { … }`; assignment is `name : value`; `?` prints;
`while`/`if` control flow; arithmetic and comparison operators; numbers (the
core numeric type) and strings; arrays. A key point of divergence from C: `n :
n + 1` is an *assignment* in Shalimar, where the same text is a goto label in C —
which is why the editor lays Shalimar out by its own indent rules.

**The runtime.** A Shalimar program links against a runtime archive
(`shmrt-<target>`), which owns `main`, the numeric formatting, the console I/O,
the array machinery and the failure paths. Every Shalimar object exports the
same three startup symbols whatever file it came from, and the runtime provides
`main` — which is why two Shalimar objects cannot simply be linked together (see
below).

**`uses` / borrowing.** Shalimar borrows library functions by name; a borrowed
name is resolved against the runtime and may not also be used as a variable. The
compiler checks that a call names something the file actually borrowed, and
reports at the point of interception rather than as a later phase.

**What Shalimar cannot do — its boundaries, by design:**

- **No block comment, no character literal, no escapes in a string literal, and
  no preprocessor.** The lexer is minimal.
- **No separate compilation into one program from two Shalimar files.** Every
  unit exports the same three startup symbols and the runtime owns `main`, and
  the language has no cross-file declarations for a call across a link to be
  checked against. A project that wants Shalimar beside C is a project that
  builds two programs (`Compiler-S/docs/LINKING.md` has the detail). This is the
  one project refusal that did not go away.
- **No debugger.** Shalimar carries no debug information on any target, by
  decision — the Debug menu turns it away by name.
- **It is not a systems language.** No pointers-as-you-know-them-in-C, no manual
  memory model, no struct/union type zoo, no direct hardware access. It is a
  numeric language with arrays, strings and functions, and its power and its
  limits both follow from that.
- **The compiler tracks the interpreter, and where they differ it is a
  conformance bug** recorded in the compiler's `docs/CONFORMANCE.md`, not a
  license to diverge.

**Targets.** `shalimar` compiles the three host targets and `tms6747`. On tms6747 a
Shalimar program runs on the `vm6747` emulator beside the C6000 runtime
(`shmrt-tms6747`), which the emulator assembles with the program, and its `.out`
links the same runtime packed as `shmrt6x.lib` (`shmrt6xd.lib` in Debug) ahead
of RTS6x. A Debug build stops itself on the C6000 too, run on `sim6747`.

--------------------------------------------------------------------------------
## 10. Header routing: `lib/` is C, `include/` is C++

The two header folders hold different sets, and the *folder decides the header's
language while the compiler decides which folder(s) it searches*:

- **`lib/`** holds the **C** standard headers (`stdio.h`, `string.h`,
  `assert.h`, …). The name is historical; the content is C.
- **`include/`** holds the **C++** standard headers (`<vector>`, `<new>`,
  `<typeinfo>`, `<cstdio>`, …).

- **`c90`** (built with `-DCC1_INCLUDE_DIR=lib`) searches **one** system
  directory: `lib/`. `#include <stdio.h>` → `lib/stdio.h`. It never looks in
  `include/`.
- **`cpp11`** (built with `-DCXX1_CXX_INCLUDE_DIR=include` and
  `-DCXX1_INCLUDE_DIR=lib`) searches **two**, in order: `include/` first, then
  `lib/`. `#include <vector>` → `include/vector`; `#include <cstdio>` →
  `include/cstdio`, which itself pulls `<stdio.h>` → `lib/stdio.h`.

Both put your `-I` directories ahead of the system ones, so a project header
wins over a standard one.

**Discovery at run time**: environment overrides (`CPP11_INCLUDE`/`CPP11_LIB`)
first; else beside the binary, then one directory up (`../include`, `../lib`),
accepting a pair only if `include/` actually has `vector` and `lib/` actually
has `stddef.h`; else the paths compiled in with the `-D` flags. That
"beside, then one up" is why the install puts compilers in `bin\` with
`include\`/`lib\` one level above, and why the Shalimar `bin\lib\` (runtime)
never collides with the top-level `lib\` (C headers) — different levels, and the
`stddef.h` probe rejects the runtime folder.
