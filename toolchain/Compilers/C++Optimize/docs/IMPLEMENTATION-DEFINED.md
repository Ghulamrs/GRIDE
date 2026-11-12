# cpp11: the implementation-defined behaviour

[intro.compliance]/2 and [defns.impl.defined]: what ISO C++11 leaves to the implementation, as cpp11 defines
it for each target. The values are the backends' own (`src/backend/*.cpp`); where a target follows a platform's
ABI it is that ABI's choice.

## Targets

| | x86_64-windows | x86_64-linux | arm64-darwin | tms6747 |
|---|---|---|---|---|
| implementation | hosted | hosted | hosted | freestanding (`__STDC_HOSTED__` 0) |
| `char` | signed | signed | signed | signed |
| `short`, `int` | 2, 4 | 2, 4 | 2, 4 | 2, 4 |
| `long` | 4 | 8 | 8 | 4 |
| `long long` | 8 | 8 | 8 | 8 |
| `wchar_t` | 2, unsigned | 4, signed | 4, signed | 2, unsigned |
| `float`, `double` | IEEE binary32, binary64 | the same | the same | the same |
| `long double` | 8 (binary64) | 16 (x87 extended) | 8 (binary64) | 8 (binary64) |
| pointers, `size_t` | 8 | 8 | 8 | 4 |
| byte order | little | little | little | little |
| C++ ABI | Microsoft (cl's) | Itanium | Itanium | Itanium (TI EABI) |
| C library | the UCRT | glibc | libSystem | RTS6x |
| `numeric_limits<int>::traps` | true | true | true | false (no trap on division by zero) |

**tms6747 is freestanding** ([compliance]/7): the headers Table 16 names are complete, `<thread>` cannot start
a thread, and the rest of the hosted library is shipped as an extension over RTS6x's C library. That extension
lacks what RTS6x lacks: `<cfenv>` (no floating-point environment), the C library's `llabs`, `atoll`, `strncat`,
`wcslen`, `copysign`, `lround`, `remquo`, `lgamma`, `tmpnam`, `setvbuf` and `vsscanf`, so a program naming
one of them does not link. `main` is entered with `argc` 0 and `argv[0]` null, as the standard allows a
program with no environment ([basic.start.main]/2).

## Language

- **A diagnostic** ([intro.compliance]/2) is `file:line:column: error:` or `warning:`, with the line and a caret
  under it, and under an error met in a template a `note:` for each instantiation that led there. Every error
  in a file is reported, up to twenty, and no code comes of the file. A warning is a diagnostic too: a floating
  literal past its type's range is ill-formed ([lex.fcon]/1), and cpp11 says so with a warning and reads it as
  infinity, as gcc and clang do. Warnings are not said of the standard headers.
- **Source and execution character sets** ([lex.charset], [lex.ccon]): source files are UTF-8; the execution
  character set of narrow literals is UTF-8, of wide literals UTF-16 (Windows, tms6747) or UTF-32.
- **A multicharacter literal** `'ab'` is an `int` whose value is the characters' codes in order, the last in the
  low byte ([lex.ccon]/1).
- **`#include <...>`** searches the compiler's `include` directory - `c++`, the C headers in `c` and, on Windows,
  `win` - then `-I` directories; `"..."` searches the including file's directory first ([cpp.include]/2-3).
- **`#pragma once`** reads a file once, **`#pragma pack`** packs a struct's members on x86_64-windows,
  x86_64-linux and arm64-darwin and is refused on tms6747, whose loads are aligned; every other `#pragma` is
  ignored ([cpp.pragma]).
- **Extended alignments** ([basic.align]/3): every power of two to 8192 is supported for objects of static
  storage duration, members and `new`; for an object of automatic storage duration on x86_64-windows,
  x86_64-linux and arm64-darwin, whose frames are realigned, and not on tms6747, where it is refused as
  [dcl.align]/5 asks.
- **`thread_local`** on tms6747, which runs one thread and has no thread storage, gives the one static copy
  ([basic.stc.thread]).
- **`std::terminate` and `abort`** end the program with status 3 on Windows and 134 on the C6747, and by
  SIGABRT on Linux and Darwin; whether open streams are flushed first is the C library's choice - Windows'
  does not, RTS6x's does ([except.terminate]).
- **Right shift of a negative value** is arithmetic; **conversion to a signed type that cannot hold the value**
  wraps modulo 2^N ([expr.shift]/3, [conv.integral]/3).
- **`sizeof(bool)`** is 1, `true` is 1 ([expr.sizeof]/1).
- **`type_info::name()`** is cl's undecorated name on Windows and the Itanium mangled name elsewhere
  ([type.info]/9).
- **`main`'s result** is the program's exit status ([basic.start.main]/5).
- **Alignment**: `alignof(max_align_t)` is 16 on x86_64-linux and 8 on the other three, as each platform's
  compiler has it ([basic.align]/2).

## Library

- **`default_random_engine`** is `mt19937`; **`random_device`** reads `rand_s` (Windows) or `/dev/urandom`,
  and on tms6747 is a clock-seeded `mt19937` whose `entropy()` is 0 ([rand.predef], [rand.device]).
- **The distributions' algorithms** ([rand.dist]/3): uniform_int by rejection, normal by Marsaglia's polar
  method, gamma by Marsaglia and Tsang, poisson by multiplication below a mean of 12 and Hormann's PTRS above.
- **`system_category()`** on Windows holds `GetLastError()`'s numbers, its messages from `FormatMessage`, the
  common ones mapped to `errc` conditions; elsewhere it is `errno`'s ([syserr.errcat.objects]).
- **`high_resolution_clock`** is `steady_clock`; `system_clock`'s period is 100 ns ([time.clock]).
- **`thread::hardware_concurrency()`** is the processors the system reports; on tms6747 threads cannot be
  started and `std::thread`'s constructor throws `system_error` with `resource_unavailable_try_again`.
- **The atomic types** are not lock-free: every operation takes one lock shared by the program, and
  `is_lock_free()` and the `ATOMIC_*_LOCK_FREE` macros say so ([atomics.lockfree]). `atomic_flag` is the
  exception [atomics.flag]/2 requires: an exchange with no lock, `xchg` on x86-64, a load-exclusive and
  store-exclusive pair on arm64, and on the one-CPU C6747 a load and a store in a branch's delay slots,
  which no interrupt can come between.
- **The locale names** ([locale.cons]/8): `"C"`, `"POSIX"` and `""` name the "C" locale; any other name throws
  `runtime_error`, from `locale`'s constructors and the `_byname` facets' alike. A program's own facets are
  used as the standard's are; the "C" locale's are made the first time one is asked for.
- **The native character sets `codecvt<wchar_t, char, mbstate_t>` converts between** ([locale.codecvt]/3) are
  the execution character sets above: UTF-8 and UTF-16 or UTF-32, on every target and in the "C" locale; so
  are `codecvt<char16_t, char, mbstate_t>`'s, `codecvt<char32_t, char, mbstate_t>`'s and `<cuchar>`'s
  conversions. A wide file stream converts through its locale's facet ([filebuf]/5).
- **`<cmath>`'s `long double` overloads** compute in `long double`'s own precision: through glibc's `l`
  functions on x86_64-linux, where it is x87's 64-bit significand, and through the double functions on the
  other three, where it is double ([c.math]).

## Limits (Annex B)

Annex B's quantities are guidelines; these are cpp11's. The set `q200` of the conformance suite holds one
program at each of Annex B's quantities (`tests/conformance/pos/q200/limits`, 2026-11-04); every one of the
41 compiles and runs on x86_64-windows, with the three remarks below. Three limits are fixed, each refused by a
message that names it, so that a program past them is refused rather than a compiler that runs out of stack:

| quantity | Annex B | cpp11 |
|---|---|---|
| nesting of blocks, parentheses, statements, lambdas and template instantiations, counted together | 256 each, 1024 instantiations | 2048 together |
| nested `#include` | 256 | 256 |
| recursive constexpr function invocations | 512 | 1024 |
| everything else Annex B lists | its value | no fixed limit: memory |

Remarks: 16,384 final overriders in a class compile, and on x86_64-windows the object they make - 49,192
COMDAT sections - is past GRIDE's link, which reads a section number as 16 signed bits (cl's link takes it with
`/bigobj`); a 16,384-deep chain of base classes compiles in five minutes, the layout being re-read per level;
256 nested class definitions compile in a second and 512 in two minutes, the member-specification likewise,
so a nesting past a few hundred is not practical though it is not refused. cl itself refuses six of the 41
(C1061 at 256 nested blocks, statements or namespaces, C1112 past 127 macro parameters, C1026 at 1024 nested
linkage specifications, C2131 below 512 constexpr calls), which the suite marks `own`.
