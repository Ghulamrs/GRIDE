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

## Language

- **Source and execution character sets** ([lex.charset], [lex.ccon]): source files are UTF-8; the execution
  character set of narrow literals is UTF-8, of wide literals UTF-16 (Windows, tms6747) or UTF-32.
- **A multicharacter literal** `'ab'` is an `int` whose value is the characters' codes in order, the last in the
  low byte ([lex.ccon]/1).
- **`#include <...>`** searches the compiler's `include` directory and its `lib` C headers, then `-I`
  directories; `"..."` searches the including file's directory first ([cpp.include]/2-3).
- **`#pragma once`** reads a file once; every other `#pragma` is ignored ([cpp.pragma]).
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
- **The atomic types** are not lock-free: every operation takes one lock shared by the program
  ([atomics.lockfree]); see EXCLUSIONS.md for atomic_flag.

## Limits (Annex B)

Annex B's quantities are guidelines; these are cpp11's.

| quantity | Annex B | cpp11 |
|---|---|---|
| nesting of blocks, parentheses, statements, lambdas and instantiations together | 256 each | 2048 together |
| nested `#include` | 256 | 256 |
| everything else Annex B lists | its value | no fixed limit: memory |
