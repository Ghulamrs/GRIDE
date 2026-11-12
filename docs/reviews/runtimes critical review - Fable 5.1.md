# GRIDE's runtimes - a critical review of grt, RTS6x, printf6x and shmrt as cpp11 builds them and as a program gets them

Reviewer: Fable 5.1. Date: 2026-11-05. Subject: `main` at b97ce82 (GRIDE3), cloned clean into
`C:\Users\GRA\AppData\Local\Temp\claude\D--GRA-My-Documents-GIT-Test\60fa293a-82ab-44be-a4ab-1b78d2445764\scratchpad\g`
(below, `REVIEW\g`) and built there with Visual Studio 2026's MSBuild (v143), the solution's **Release** and **Debug**
configurations both, projects `Windows\grt`, `TI\rts6x`, `TI\sim6747`, `TI\vm6747`, `TI\lnk6x`, `Windows\gride-dbg` and
what they depend on (c90, cpp11 1.61, shalimar 1.3, masm, link, asm6x 1.4, lnk6x 1.2, ar6x). Nothing in GRIDE3 itself was
built, run or changed. Every claim below is backed by a program under `REVIEW\p\` (x86_64-windows), `REVIEW\p6\`
(tms6747) or a command; the runners are `REVIEW\w.ps1` (one source, four ways: grt Release, grt Debug, Microsoft's CRT
Release and Debug, each exactly as GRIDE's engine compiles and links it), `REVIEW\c6.ps1` (tms6747 Release over
`rts6x.lib` and Debug over `rts6xd.lib`, run by `sim6747 --run -c`) and `REVIEW\sh.ps1` (a Shalimar source eight ways);
outputs are beside each program in `REVIEW\out\`, `REVIEW\out6\` and `REVIEW\outs\`. `docs\Runtime_Design.md` and the test
transcripts were read as claims and tested, not repeated. The frame, widened at the user's word during the review: the six
libraries - **grt, rts6x, printf6x, rts6xd, printf6xd and shmrt** - are each a client of cpp11 across the whole of GRIDE3,
and are reviewed as such, the Debug ones as libraries in their own right (section 4); the cl-built `msvc\shmrt[d].lib`
pair is not cpp11's and serves as comparison only.

## 1 Verdict

**grt, on Windows, does what its design promises for a correct program.** Across 20 probe programs (stdio formats,
strtod and scanf, files, the heap, time, locale, math, signals, setjmp, threads, the C++ ABI, exceptions, RTTI,
iostreams, Shalimar) the same cpp11 objects linked with GRIDE's runtime and with Microsoft's CRT printed the same bytes
and ended with the same exit codes, in Release and in Debug, with **one byte-level difference** found (`ftell` after
`ungetc` at the start of a file, finding 11) and two places where grt is more lenient than the UCRT (finding 12). A
program imports `KERNEL32.dll` alone (37 functions for hello world against the UCRT's 76), is a third smaller, starts
threads through cpp11's `<thread>` and keeps its lines whole, and the Release libraries are byte-identical whichever
solution configuration built them. That is a real achievement for a runtime nine weeks old.

**What a finished application gets is nonetheless worse than the design says, in this order of harm:**

1. **On the C6747, "Release" is not an optimised build.** c90 writes the same code at `-O2` as at `-O0` for tms6747
   (sieve.c: 762 identical lines), and cpp11 silently drops `-O2` for any C++ file that includes `<string>`, `<vector>`,
   `<exception>`, `<limits>`, `<memory>` or `<type_traits>` (a `volatile` in the header), so a C++ program's Release build
   is byte-for-byte its `-O0` build (strO2.cpp: 5,607 identical lines; cxx6b.cpp: 13,966). Three of the runtime's own
   members are built that way too (`rts6x.lib`'s `BadAlloc`, `shmrt6x.lib`'s `Console` and `Failure`), and the MSBuild
   tasks swallow the warning that says so (finding 1).
2. **Static destructors and `atexit` functions are silently dropped past a fixed count:** 64 on the C6747, 4,096 on
   Windows. cpp11 ignores `atexit`'s failure, so the 65th namespace-scope object with a destructor on the C6747 (and
   the 4,097th on Windows) is never destroyed and nothing says so (finding 2). The design says the list "grows"; it is an
   array.
3. **The Debug runtime's allocator is ten times slower than Microsoft's debug heap** on large blocks, because it fills
   every block byte by byte in `-O0` c90 code: `malloc(1 GB)` takes 12.6 s against 1.1 s, 100 × `malloc(10 MB)` 11.6 s
   against 1.1 s. A C++ probe that runs in 2 s in Release did not finish in 60 s on the Debug runtime (finding 3).
4. **The header set is a strict subset of Microsoft's, and `_exit` is not even defined.** A program written for MSVC
   that uses `_exit`, `_isatty`, `_open`/`_read`/`_write`, `_dup`, `_putenv`, `_getpid`, `_strdup`, `_stricmp`,
   `_snprintf`, `_itoa`, `_beginthreadex`, `_fdopen`, `_tzset`, `_controlfp`, `_isnan`, `_msize`, `_aligned_malloc`,
   `alloca`, `_splitpath`, `_fullpath`, `_stat` or any `_s` function does not compile with GRIDE's headers for either
   runtime (finding 4).
5. **Nothing but GRIDE's engine ever links grt.** `cpp11 prog.cpp -o prog.exe`, the compiler's own driver, links
   Microsoft's static CRT through `LIB` with an 8 MB stack and fails without Visual Studio ("libcmt.lib: cannot open")
   (finding 5).
6. **Release programs cannot be debugged into the runtime, and Shalimar's Debug runtime has no lines at all.** The
   Release libraries carry no CodeView (Microsoft's `libcmt.lib` has `.debug$S` in 1,189 of 1,190 members), and
   `shmrtd.lib` over grt and `shmrt6xd.lib` are compiled without `-g` (finding 6).
7. **The C6747 link defaults are tight and unguarded:** a 16 KB stack and a 1 MB heap on a 64 MB RAM map, no stack
   check. Two hundred recursive calls with 256-byte frames corrupt memory and the simulator stops on an undefined
   instruction (finding 7).

Found on the way, in cpp11, and visible to every program on either runtime: a namespace-scope object with a trivial
constructor and a non-trivial destructor is **never destroyed** (both targets); `std::map<std::string, int>::operator[]`
**does not link for tms6747** (an unresolved local label); a C++ program that `longjmp`s out of a SIGSEGV handler
**hangs** (the same program in C runs). Findings 9, 10 and 13.

**Recommendation.** For the C6747, give c90 an optimiser or stop calling the configuration Release; make cpp11's
`volatile` rule per function on tms6747 as it is on x86_64 (1.40), and let the build tasks print the compilers'
warnings. Make `Termination`'s list grow (or fail loudly: cpp11 should check `atexit`'s answer). Fill the Debug heap
with `memset`. Define `_exit` and decide which Microsoft names the headers will carry, since "both runtimes" today
means "neither". Give the C6747 a larger default stack. Then the three cpp11 items.

## 2 What was run

| Probe | Where | What it checks |
|---|---|---|
| `p\hello.c`, `p\hello.cpp` | 4 ways | size, imports, stack, exit code |
| `p\stdio2.cpp` | 4 ways | 40 lines of printf formats: widths, flags, `%a`, NaN and infinity signs, ties, `%zu`/`%td`/`%I64d`/`%hhd`, `%ls`, snprintf counts, `%c` of 0 |
| `p\strto.cpp` | 4 ways | 50 strtod/strtol/strtoul/strtoll edge cases, 18 sscanf cases, ctype |
| `p\files.c`, `p\filesm.c`, `p\files2.c`, `p\badmode.c` | 4 ways | text and binary modes, CR LF, CTRL+Z, positions, ungetc, w+/a/a+/r+, buffering seen through a second handle, 5,000-byte fwrite, tmpfile, tmpnam, freopen, errors |
| `p\heap.c`, `p\bigalloc.c` | 4 ways | malloc(0), overflow, realloc, alignment, 2,000 mixed blocks, Debug fill bytes, 200 × 10 MB; timings |
| `p\cxx1.cpp` | 4 ways | static construction order, local statics, virtual-base dynamic_cast, typeid names, bad_cast, exceptions through constructors and unwinding, rethrow, new_handler and bad_alloc, exception_ptr, map, string, iostreams |
| `p\misc.cpp`, `p\sigjmp.c`, `p\sigjmp.cpp` | 4 ways | rand, qsort, mktime/localtime/strftime/asctime, clock, getenv, math errno and values, strerror, strtok, setlocale, div, setjmp/longjmp from a signal handler |
| `p\thr.cpp` | 4 ways | 6 `std::thread`s with a mutex, an atomic, thread_local, per-thread errno, 2,100 lines to one stdout |
| `p\stdin1.c` | 4 ways, piped stdin | scanf and fgets from a pipe with CR LF, EOF, clearerr |
| `p\ends.cpp` | 4 ways × 18 endings | abort, assert, uncaught throw, pure virtual call, null FILE *, null format, exit, _Exit, quick_exit, access violation, division by zero, stack overflow, terminate, raise, signal(3), `%n` |
| `p\atexitcap.c`, `p\uexit.c`, `p\trivdtor.cpp` | 4 / 2 ways | 5,000 atexit registrations; `_exit`; a static object with a trivial constructor |
| `p6\hello6.c`, `mem6.c`, `files6.c`, `cxx6b.cpp`, `cxx6c.cpp`, `map6.cpp`, `map6b.cpp`, `atexit6b..e.cpp`, `trivdtor.cpp`, `strO2.cpp` | sim6747, rts6x and rts6xd (and printf6x first) | the same questions for the C6747: formats, heap limits, stack, host files, exceptions and RTTI, destructor counts, -O2 |
| `shl-primes\numbers.shl` | 8 ways | Shalimar over grt and Microsoft's, Release and Debug; sim6747 and vm6747 |
| library scans (`REVIEW\p\pe_imports.py`, inline Python) | - | members, `.debug$S`/DWARF presence, printf6x against rts6x, Debug-configuration against Release-configuration libraries, the PDB of a `/debug` link |

Every probe's four Windows ways agreed byte for byte on stdout, stderr and exit code except where a finding below says
otherwise; every tms6747 probe printed the same over `rts6x.lib` and `rts6xd.lib`. Microsoft's Debug CRT answers an
assert or an invalid parameter with a dialog, so its runs of those endings were killed after 40 s and read as
0xFFFFFFFF.

## 3 How each library is really compiled

Read from `toolchain\msbuild\Toolchain.targets`, `toolchain\Windows\RT\grt.vcxproj`, `toolchain\TI\RTS6x\rts6x.vcxproj`
and `toolchain\Compilers\Compiler-Si\ide\shc.vcxproj`, and checked against the libraries the build left.

| Library | Compiler and flags | Packed by | Debug information | Notes |
|---|---|---|---|---|
| `grtstart`, `grtcompat`, `grtcxx`, `grtc`, `grtwin` | `.c` by c90, `.cpp` by cpp11, each `-S -arch x86_64-windows -masm=masm -nologo -O2 -DNDEBUG=1 -DRTS6X_REFERENCE=1 -I internal -I RTS6x\src`; `.asm` straight; GRIDE's masm `-t x64` | GRIDE's `link /LIB` | none (`.debug$S` in 0 of 268 grtc members) | `RTS6X_REFERENCE` is defined but selects nothing on Windows (the `*-reference.cpp` files are taken out by `rts6x-c.sources`) |
| the `d` variants | the same, `-O0 -g -DGRT_DEBUG=1 -DRTS6X_REFERENCE=1` | the same | CodeView in 250 of 268 grtcd members | `GRT_DEBUG` changes exactly two files: `c\abort.cpp` (message and exit 3) and `win\heap.c` (header, guards, fill bytes); everything else differs only by `-O0 -g` |
| `kernel32.lib` | `link /LIB /DEF:kernel32.def` | - | - | 93 members |
| `shmrt.lib` (over grt) | cpp11 `-O2`, no `-DNDEBUG` | `link /LIB` | none | |
| `shmrtd.lib` (over grt) | cpp11 `-O0 -DSHM_DEBUG=1`, **no `-g`** | `link /LIB` | **none** (0 of 40 members) | finding 6 |
| `msvc\shmrt.lib`, `msvc\shmrtd.lib` | cl `/std:c++14 /W4 /WX /EHsc /permissive- /O2`, `/MT` or `/MTd /DSHM_DEBUG=1` | lib.exe | cl's `.debug$S` only, no `/Zi` | the Debug one is `/O2` too |
| `rts6x.lib` | cpp11 `-S -arch tms6747 -nologo -O2 -DNDEBUG=1 -I src\internal`; `.s` straight; asm6x | ar6x | none | 371 members; `BadAlloc.obj` is compiled at `-O0` (finding 1) |
| `rts6xd.lib` | `-O0 -g -D_DEBUG=1` | ar6x | DWARF in 334 of 371 | `_DEBUG` is tested by no RTS6x source: rts6xd is rts6x unoptimised, nothing more |
| `printf6x.lib`, `printf6xd.lib` | the same objects | ar6x | - | 26 members: 25 are byte-identical copies of rts6x members, the 26th is `eh-none\pr3.obj`, a stub personality that the real unwinder never calls; linking it first or not at all changes nothing (`cxx6c.cpp`) |
| `shmrt6x.lib`, `shmrt6xd.lib` | cpp11 `-O2` / `-O0 -DSHM_DEBUG=1` **without `-g`**, written by shc's build as `.s` | asm6x, ar6x | **none** | `Console.cpp` and `Failure.cpp` are compiled at `-O0` in the Release library (finding 1) |

**The two solution configurations.** The Release configuration's cpp11, c90, masm and link and the Debug configuration's
(unoptimised tools) produce byte-identical `grtstart`..`grtwin`, `kernel32`, `shmrt`, `shmrtd`, `rts6x`, `printf6x[d]`,
`shmrt6x[d]`. The Debug variants differ: `grtcd.lib` in 12 members, `grtcxxd.lib` in 3, `rts6xd.lib` in 2. Two causes,
both in debug information only: the compiler's own `include\` folder (`x64\Release\include\...` against
`x64\Debug\include\...`) is written into the line tables, and **the Debug build of masm.exe writes a `.debug$S` four
bytes shorter than the Release build's from the same `.s`** (`MathConstants.obj`: `.rdata` identical, `.debug$S` 256
against 252 bytes). Code and data are the same; a user who builds the solution's Debug configuration gets a Debug runtime
that is not the bytes the Release configuration ships, but behaves the same.

**The build checks hold:** LibraryClosed, FrameCheck and CatalogueCheck pass in both configurations; the catalogue has
314 entries, all done for x86_64-windows, 180 for tms6747.

## 4 The six libraries as cpp11's clients

What cpp11 does to each of them, what each has had to do about cpp11, and what reached each in this review. The
measurements: instruction lines are the `.s` lines that are neither directives nor comments, compiled from `REVIEW\g`
with the flags each project uses; member counts from the libraries both configurations built; mentions of the
compiler from `grep -rn cpp11` over each library's sources.

**The common thread.** cpp11 serves its clients unequally by target. For x86_64-windows it gives `-O2` to every file,
decides `volatile` per function, and accepts `-g` with `-O2` - at a price the Release libraries refuse to pay: with `-g`
it inlines nothing, so the same `-O2` compile of `Formatter.cpp` drops from 769 instruction lines to 598, `Print.cpp`
from 9,112 to 8,355, `StreamCore.cpp` from 1,474 to 1,405, shmrt's `Console.cpp` from 5,330 to 4,744 (c90 goes the other
way: `files.c` 2,818 to 3,249), and the design measured 12-29 % on fgets and sscanf. For tms6747 it gives `-O2` only to a
file that never reaches a `volatile` (finding 1), and **refuses `-g` with `-O` outright** ("`-g with -O is not supported
yet for tms6747 - the C6000 scheduler does not carry a source position through its packets; compile with -g alone`"),
so there a Debug library is `-O0` by necessity and a Release one is `-O0` for most of C++ by accident. The clients
answer in their sources: 21 mentions of cpp11 in grt's own files and 15 in RTS6x's, nearly all of them workarounds or
accommodations for its code (below), and 1 in shmrt's. The MSBuild tasks that run cpp11 for all six keep its warnings
from everyone.

### grt - `grtstart`, `grtcompat`, `grtcxx`, `grtc`, `grtwin`, and the `d` five

- **cpp11's share.** Every `.cpp`: the 268 members of `grtc` (RTS6x's C library through `rts6x-c.sources` and grt's
  own `c\`), the 21 of `grtcxx` (RTS6x's allocation through `rts6x-cxx.sources`, grt's RTTI, statics, thread_local,
  exceptions), `win\errno.cpp`. c90 compiles the `.c` of `start\`, `compat\` and `win\` (20 of `grtwin`'s 22 members),
  GRIDE's masm the `.asm`. Release: `-O2 -DNDEBUG=1 -DRTS6X_REFERENCE=1`, no `-g`; Debug: `-O0 -g -DGRT_DEBUG=1`.
- **What grt does about its compiler** (its own comments): `grt::bytesBetween` because cpp11 writes an `idiv` by 1 for
  a difference of `char` pointers; `Print.cpp`'s reciprocals because it divides by a constant with `div`; `ehcall.asm`
  keeping rbp because a cpp11 catch funclet stores a fifth argument over its parent's saved rbp; `tlsdata.asm` because
  cpp11 cannot place a word in a section of its choosing; `rtti.cpp` reading offsets as 64-bit numbers because 1.29
  negated `p - unsigned` in 32 bits; `support.cpp` because cpp11 calls `_Init_thread_header` every time; `qsortword.asm`
  because cpp11 kept five values in memory around every comparison. RTS6x's shared files add `DecimalDigits.cpp`'s
  reciprocals, `IntegerDigits.h`'s operand order, `MathConstants.h`'s data members "rather than inline functions: cpp11
  loads one where it is named", `MathBits.h` ("a 64-bit integer costs cpp11 a generic shift for every >> 32"),
  `Stream.h` ("out of line, as cpp11 emits every inline member"), `ftable.cpp` (a data-only translation unit, "which
  cpp11 takes since 2026-10-06"). The client is carrying its compiler's optimiser.
- **The Debug five** are the same sources at `-O0 -g`; `GRT_DEBUG` reaches two files (`c\abort.cpp`, `win\heap.c`).
  CodeView in 250 of 268 `grtcd` members, 18 of 21 `grtcxxd`, 20 of 22 `grtwind`; none in the Release five. The two
  solution configurations give byte-identical Release libraries and Debug ones that differ in 15 members, in debug
  information only (finding 8).
- **What reached it here:** findings 2 (4,096), 3 (the Debug heap, c90's), 4, 5, 6, 8, 11, 12; cpp11's 10 and 13 pass
  through it untouched. Everything else probed matched Microsoft's CRT (section 6, below the findings).

### rts6x

- **cpp11's share.** 348 of 371 members, `-S -arch tms6747 -nologo -O2 -DNDEBUG=1 -I src\internal`; 23 are assembly
  (`boot`, `cio`, `setjmp`, the C6000 ABI's division and conversion helpers, `memcpy`/`memset`/`strlen`, the
  personality stubs). One of the 348, `cxx\BadAlloc.obj`, is compiled at `-O0` because `<new>` reaches `<exception>`'s
  `volatile`, and the build says nothing (finding 1).
- **What RTS6x does about its compiler:** the 15 mentions above, plus `WideDivision.cpp` ("cpp11 left a branch's end
  label undefined on the C6000 at -O2", the same family as finding 9), `LogarithmNatural.cpp` (an evaluation order cpp11
  for x86-64 took the wrong way), `ExceptionThrow.cpp` ("cpp11 refuses throw(T)"), `TrigonometricEvaluate.cpp` written
  around what cpp11 keeps in saved registers.
- **What a program gets from it:** the probes of section 2 all ran and printed what Windows prints, text mode, positions
  and exceptions included; hello world loads 62,920 bytes. The C6747 is where cpp11's own limits land on the client:
  finding 1 (no optimisation for most C++, none at all from c90), 2 (64 entries), 7 (the defaults), 9 (`map`), 10.

### rts6xd

- **A library in its own right:** the same 371 members at `-O0 -g -D_DEBUG=1`, 3,143,584 bytes of members against
  rts6x's 1,096,592. `_DEBUG` is tested by no RTS6x source, so nothing in it behaves differently: it is rts6x without
  the optimiser, with DWARF in 334 members. The 37 without are the 23 assembly members and 14 data-only ones
  (`AtanTable`, `ExpTable`, `LogTable`, `MathConstants`, `PowersOfFive`, `ReciprocalTable`, `SinCosTable`, `Random`,
  `NumberText`, `ErrorNumber`, ...), and those 37 are byte-identical to rts6x's.
- **As cpp11's client** it is the only form of the library cpp11 can give lines to, since it refuses `-g` with `-O` on
  this target; so a Debug program on the C6747 pays `-O0` in its runtime as well as its own code, and because of
  finding 1 that is often the same code a Release program gets. hello world loads 101,240 bytes against 62,920.
- **Checked:** every tms6747 probe printed the same over rts6xd as over rts6x (`c6.ps1`'s `dbg = rel` on each), the
  Debug `cxx6b` with exceptions and RTTI included. The two solution configurations' rts6xd differ in two members
  (`BadAlloc`, `_gride_shortest_double`), in line-table paths.

### printf6x

- **What it is:** 26 members, 105,340 bytes. 25 are byte-for-byte the rts6x members `printf6x.members` names (`host\`,
  `stdio\Format*`, `DecimalDigits`, `IntegerDigits`, `OutputSink`, `StreamFill`, `StreamWrite`, `fprintf`, `printf`,
  `sprintf`, `wprintf`, `ftable`, the integer division helpers, `memcpy`, `memset`, `strlen`); the 26th is
  `eh-none\pr3.obj`, a stub `__c6xabi_unwind_cpp_pr3` answering "cannot unwind", which rts6x's real unwinder never
  calls. So as cpp11's client it adds nothing to rts6x: the same compiles, the same `-O2`.
- **What a program gets from it:** nothing it would not get from rts6x. Linked alone it cannot make a program
  (`lnk6x: entry point not found: _c_int00`); linked first, before rts6x, `cxx6c.cpp`'s exceptions and output are
  unchanged. toolchain.json links it "when printf"; cpp11's own driver never names it (`Driver.cpp`: `-l rts6x.lib`
  only). It exists for the CCS names (`RTS6x.lib`, `Printf6x.lib`) and costs nothing; it should not be mistaken for a
  separate printf.

### printf6xd

- The same 26 relative to rts6xd (25 identical members and `pr3.obj`), 324,036 bytes, DWARF where rts6xd has it. A
  Debug C6747 program linking `rts6xd.lib printf6xd.lib` takes every printf member from whichever comes first and gets
  the same bytes either way.

### shmrt - `shmrt.lib`, `shmrtd.lib` over grt; `shmrt6x.lib`, `shmrt6xd.lib` for the C6747

- **cpp11's share: all of it, four ways.** x86_64-windows `-O2` (39 members: `Failure`, `Numbers`, `Array`, `Console`,
  `Runtime`, `Borrow` and the 33 generated `wrappers\shm_c_*.cpp`) and `-O0 -DSHM_DEBUG=1` (40, with `Debug.cpp`);
  tms6747 `-O2` and `-O0 -DSHM_DEBUG=1`, written by shc's build as `.s` and packed by rts6x's project. **No build of it
  has `-g`** (finding 6): 0 of 40 members of `shmrtd.lib` carry CodeView, 0 of 40 of `shmrt6xd.lib` DWARF, where a
  Debug C program's runtime has lines. On the C6747 `Console.cpp` and `Failure.cpp`, which use `std::string` and
  `std::vector`, are compiled at `-O0` in the Release library (finding 1).
- **What shmrt does about its compiler:** one mention. Its wrappers are one file each so that a program links only
  what it borrows, a shape chosen because cpp11 writes a whole file into one code section (the design's "One member to
  a wrapper").
- **Checked:** `numbers.shl` prints the same lines over `shmrt.lib`+grt, `shmrtd.lib`+grt Debug, under sim6747 with
  `shmrt6x[d].lib`+`rts6x[d].lib`, and run straight by vm6747 from `lib\tms6747\shmrt[-debug]`. A Debug Windows build
  needs shalimar's `-masm=masm` with `--debug -g`, as GRIDE passes it; its GNU spelling is for clang.
- **The comparison, `msvc\shmrt.lib` and `msvc\shmrtd.lib`** (cl `/O2 /MT` and `/O2 /MTd /DSHM_DEBUG=1`, 40 and 41
  members, cl's `.debug$S` in each but no `/Zi`, member names by full path so the two configurations' libraries differ
  in names only): `numbers.shl` over them prints the same as over cpp11's. The Debug one is optimised where cpp11's is
  not, and neither has lines. The comparison shows nothing cpp11's build lacks in behaviour, and one thing it lacks in
  form: cl gives each function a COMDAT, so Microsoft's link drops unused wrappers from one member where GRIDE's
  needed the split into 33 files.

## 5 Findings, ranked by harm

### 1 On the C6747, Release is an unoptimised build

**c90 ignores `-O2` for tms6747.** `examples\toolchain-demo\sieve.c` compiled `-arch tms6747` at `-O2` and at `-O0` gives
the same 762 lines; for x86_64-windows the two differ (237 against 434 lines). `p6\hello6.c` the same (345 lines).

**cpp11 compiles any C++ file that reaches a `volatile` at `-O0`, with a warning**, on tms6747 (on x86_64 this has been
per function since 1.40). cpp11's `<type_traits>`, `<exception>`, `<limits>`, `<memory>` and `<atomic>` contain the
word, so `<string>`, `<vector>`, `<map>`, `<iostream>` and `<new>` bring it in:

```
cpp11 -S p6\strO2.cpp -arch tms6747 -O2      ->  ...include/c++/type_traits:39:43: warning: 'volatile' accesses are not yet told
                                                  apart from plain ones by the optimiser, so strO2.cpp is compiled without -O2
fc strO2-O2.s strO2-O0.s                      ->  identical (5,607 lines); for x86_64-windows 4,334 of 7,266 lines differ
fc cxx6b-O2.s cxx6b-O0.s                      ->  identical (13,966 lines)
```

So a Release C++ program for the C6747 is its Debug program without line tables, and every performance figure for
that target is an `-O0` figure. The runtime itself is touched: compiling every RTS6x source as `Rts6xVariant` does shows
one member built that way (`cxx\BadAlloc.cpp`), and shmrt's C6747 build two (`Console.cpp`, `Failure.cpp`). **Nobody
sees the warning**: `Rts6xVariant`, `WindowsLibrary` and `RunTool Quiet="true"` log a compiler's output only when it
exits non-zero. Command: the loop in `REVIEW\vol` (`cpp11 -S <each RTS6x .cpp> -arch tms6747 -O2 -DNDEBUG=1 -I src\internal`,
grep "without -O2"): 348 files, 1 hit; shmrt's 39 files, 2 hits; grt's Windows sources, 0.

### 2 Static destructors and `atexit` functions are dropped silently past 64 (C6747) and 4,096 (Windows)

`exit\Termination.h`: `enum { Capacity = 4096 }` under `_WIN32`, `64` elsewhere; `add` answers -1 when full. cpp11's
initialisers register each namespace-scope destructor through `atexit` (Windows) or `__cxa_atexit` (tms6747) and
ignore the answer.

`p6\atexit6d.cpp` (63 objects with destructors, one more object, one `atexit`) prints `atexit function ran`;
`p6\atexit6e.cpp` (65 objects) prints `65 objects made, atexit answered -1` and then nothing: the `atexit` function and
the destructors of the objects past the 64th never run, and no message says so. `p\atexitcap.c` on Windows: 5,000
registrations, 905 refused, 4,095 run; on Microsoft's CRT 0 refused, 5,000 run. The design (Termination.h's comment)
says the Windows list "holds every static object's destructor a C++ program registers, as that runtime's grows to"; it
does not grow. Sixty-four is within reach of an ordinary C++ program on the C6747 (every global `std::string`, every
function-local static with a destructor counts).

### 3 The Debug allocator is an order of magnitude slower than Microsoft's debug heap

`win\heap.c`'s Debug half fills each block with 0xCD, 0xDD and the guards in `grt_heap_fill`, a `while (n--) *p++ = v`
loop that c90 compiles at `-O0`, instead of the runtime's own `memset`. `p\bigalloc.c` (times by `clock()`):

| | grt Release | grt Debug | Microsoft Release | Microsoft Debug |
|---|---|---|---|---|
| 100 × malloc/free 10 MB | 4 ms | **11,600 ms** | 3 ms | 1,077 ms |
| malloc/free 1 GB | 0 ms | **12,624 ms** | 0 ms | 1,096 ms |
| 1,000,000 × malloc/free 64 | 31 ms | 921 ms | 84 ms | 402 ms |
| calloc 256 MB | 0 ms | **3,058 ms** | 0 ms | 431 ms |

`p\cxx1.cpp`, which allocates 1 MB blocks until `bad_alloc` (27 GB of commits), runs in about 2 s on grt Release and on
both of Microsoft's; on grt Debug it did not finish in 60 s. A Debug build of any program that works in large buffers
pays this on every allocation. The fill bytes themselves match Microsoft's (`p\heap.c`: fresh `CD CD`, freed `DD`).

### 4 The headers lack the Microsoft and POSIX names a ported program uses, and `_exit` is not defined

The one header set (`toolchain\include\c`, `win`) declares, of 190 common Microsoft/POSIX names probed with `grep -w`,
only `getcwd`, `mkdir`, `fileno`, `__argc`, `__argv`, `perror`, `_access`/`access` and `stat`. Not declared, so a
program using them compiles for **neither** runtime: `_exit`, `_isatty`/`isatty`, `_open`, `_close`, `_read`, `_write`,
`_lseek`, `_dup`, `_dup2`, `_putenv`/`putenv`, `_getpid`, `_strdup`/`strdup`, `_stricmp`/`stricmp`, `_strnicmp`,
`_snprintf`, `_vsnprintf`, `_itoa`, `_ultoa`, `_strlwr`, `_strrev`, `_kbhit`, `_getch`, `_popen`, `_beginthread`,
`_beginthreadex`, `_endthreadex`, `_unlink`, `rmdir`, `chdir`, `_fdopen`, `_fstat`, `_telli64`, `_lseeki64`, `_commit`,
`_chsize`, `_get_osfhandle`, `_flushall`, `_filelength`, `_timezone`, `_tzset`, `_daylight`, `_tzname`, `_ftime`,
`_mkgmtime`, `_strtoi64`, `_atoi64`, `_rotl`, `_byteswap_ulong`, `_countof`, `_set_errno`, `_sleep`, `_onexit`,
`_cexit`, `_environ`, `_pgmptr`, `_set_invalid_parameter_handler`, `_controlfp`, `_control87`, `_clearfp`, `_isnan`,
`_finite`, `_copysign`, `_hypot`, `_matherr`, `_msize`, `_expand`, `_aligned_malloc`, `_aligned_free`, `alloca`,
`_alloca`, `_resetstkoflw`, every `_s` function (`strcpy_s`, `fopen_s`, `sprintf_s`, `localtime_s`, ...), `_wfopen`,
`_wcsicmp`, `_splitpath`, `_makepath`, `_fullpath`, `_getdcwd`, `_chmod`, `_stat`, `_stat64`, `_fsopen`, `_sopen`,
`_setmaxstdio`, `_cprintf`, `_putch`, `_set_printf_count_output`. The design's "the headers name only what both
runtimes provide" (5.2) reads, for a program coming from MSVC, as "a subset the UCRT exceeds everywhere".

`_exit`, which section 7 of the design lists among the ways a program ends, is neither declared nor defined:
`p\uexit.c` (`void _exit(int); ... _exit(4);`) links on Microsoft's CRT and exits 4; on grt `link: unresolved external
symbol: _exit`.

### 5 Only GRIDE's engine links grt; the compilers' own drivers link Microsoft's CRT

`Driver.cpp` (C++Optimize) ends a `cpp11 x.cpp -o x.exe` build with
`link /nologo /subsystem:console /stack:8388608 /out:... x.obj libcmt.lib libcpmt.lib libucrt.lib libvcruntime.lib
kernel32.lib legacy_stdio_definitions.lib`. With Visual Studio's `LIB` set, `cpp11 p\hello.cpp -o hello-driver.exe` gives
a program of Microsoft's CRT (76 kernel32 imports, 8 MB stack reserve - `REVIEW\p\pe_imports.py`); with `LIB` empty it
stops: `link: libcmt.lib: cannot open`. So the design's first goal, a program that builds "on any machine with Windows
and GRIDE's installation, without Visual Studio", holds through GRIDE's engine only; the command line it shows the user
(`shownRun`) and the conformance gate link a different runtime, with eight times the stack (the 1 MB default is what
GRIDE's link gives, the same as Microsoft's link; cpp11's suite case `op11_recursion_tail` passes only with the 8 MB).

### 6 Release programs cannot be debugged into the runtime; Shalimar's Debug runtime has no lines

By design (9.3) the Release variants carry no CodeView: a `/debug` link of hello over `grtstart..grtwin` gives a PDB in
which `printf.cpp` and `StreamCore` do not occur, where the same link over the `d` libraries gives them (the PDBs are in
`REVIEW\out\hello`). Microsoft's `libcmt.lib` ships `.debug$S` in 1,189 of its 1,190 members, so a Release program on
the UCRT shows the runtime's function names in a stack, and one on grt shows only its public symbols. The reason given,
that cpp11 writes slower code under `-g`, is a compiler limitation the runtime is paying for in every Release program.

`shmrtd.lib` over grt is built `-O0 -DSHM_DEBUG=1` with no `-g` (shc.vcxproj's `Flags`), `shmrt6xd` the same: 0 of 40
members carry CodeView, 0 of 40 DWARF. A Debug Shalimar program's runtime has no lines for gride-dbg's "Step into
GRIDE's runtime" to stop on, where a Debug C program's has.

### 7 The C6747 defaults: a 16 KB stack and a 1 MB heap, nothing checked

`linkerCommandFiles.ti-link` in toolchain.json (and the demo) writes `--stack_size=0x4000 --heap_size=0x100000` on a
`RAM` of 64 MB. `p6\mem6.c -DSTACK=1`: `deep(40)` with 256-byte frames answers 820; `deep(200)` ends with

```
sim6747: at 0xc0103148, cycle 221108: an undefined instruction word (deep+8)
CYCLES ... exit=none status=70
```

the stack having overwritten what lies below it; there is no guard page and nothing a program can test. The heap
gives 64 blocks of 16,000 bytes (1,024,000 bytes) and then null, with `errno` 0 (`malloc` on RTS6x sets no `ENOMEM`;
grt sets 12). `cxx6b.cpp`'s `new char[100000]` loop reaches `bad_alloc` after 10. Windows gives the same program a
1 MB guarded stack and the process heap. Sixty-three of the 64 MB are unused by default.

### 8 Debug-configuration libraries are not the Release configuration's bytes

Section 3: `grtcd.lib` (12 members), `grtcxxd.lib` (3) and `rts6xd.lib` (2) differ between the two solution builds -
line-table paths through the compiler's own `include\` folder, and masm.exe's Debug build writing four bytes less of
`.debug$S` than its Release build (`REVIEW\vol\mc-masm-Release.obj` against `mc-masm-Debug.obj` from one `.s`). Code
and data are identical, so this is a reproducibility matter: a library's bytes depend on which configuration of the
tools wrote them, and the design's "byte-identical" comparisons across releases hold only within one.

### 9 cpp11's `<map>` with a `std::string` key does not link for the C6747

`p6\map6.cpp`:

```
std::map<std::string, int> m; m["a"] = 1;
lnk6x: unresolved symbol: L$_ZNSt4pairIKSt12basic_string...St14__cxx1_indices...$$chain22
```

at `-O2` and at `-O0`; `m.insert(std::make_pair(std::string("a"), 1))` (`map6b.cpp`) links and prints 1, and the same
`operator[]` links and runs for x86_64-windows. A local label of the piecewise `pair` constructor is referenced and
never defined by cpp11's tms6747 backend. The runtime is not involved, but it is the first thing a C++ program on the
C6747 meets.

### 10 A static object with a trivial constructor is never destroyed (cpp11, both targets)

`p\trivdtor.cpp`: `struct TrivialCtor { ~TrivialCtor() { puts(...); } }; static TrivialCtor a; TrivialCtor c;` prints
`main` and the destructor line of the object that also has a user constructor, and **nothing for `a` or `c`**, on grt,
on Microsoft's CRT and on sim6747 alike: cpp11 registers no destructor for an object whose initialisation is trivial.
A global lock guard, a file closer, a profiler that reports in its destructor: none of them runs. The runtime is not the
cause (nothing is registered), but no runtime can repair it. `p\ends.cpp` shows it a second way: its `struct Static {
~Static() { printf("static dtor ran\n"); } } st;` prints nothing on a normal return or `exit(9)`, on all four ways.

### 11 `ftell` after `ungetc` at the start of a file answers -1

`p\filesm.c` step 8: `rewind(f); ungetc('Z', f); ftell(f)` is **0** on the UCRT and **-1** on grt (Release and Debug).
The only byte-level disagreement found in the stdio probes; a loop that pushes back a character and then records the
position gets an error where it did not before.

### 12 Where grt is more lenient than the UCRT, and so prints different bytes

`p\badmode.c`: `fopen("t1.txt", "q")`, `"rw"`, `""` give NULL with `errno` 22 on grt and **end the program**
(0xC0000409, an invalid parameter) on the UCRT; `fopen("", "r")` gives NULL and `errno` 2 on grt, 0xC0000409 on the
UCRT; `"r+b+"` is accepted by grt (it opens the file) and refused by the UCRT. `fclose(NULL)` fails fast on grt
(0xC0000409 at the line after the four `fopen`s), as 4.3 says the UCRT does. The lenient answers are the better ones, but the design's rule is "the same
bytes", and a program that relied on the UCRT stopping it will run on here. On the C6747, `remove` and `rename` of a
missing file answer -1 with `errno` 0 (`files6.c`; the design records it) and a failed `malloc` leaves `errno` 0.

### 13 A C++ program that longjmps out of a SIGSEGV handler hangs (cpp11, both runtimes)

`p\sigjmp.c` (c90): `signal(SIGSEGV, h); *(volatile int *)0 = 1;` with `longjmp` in the handler prints
`SIGSEGV caught, longjmp'd out with 1` on grt and on Microsoft's CRT. The same source as `p\sigjmp.cpp` (cpp11) prints
`signal: ` and never returns, on both runtimes (killed after 60 s); cl's build of the same C++ runs to the end. The
runtime delivers the signal (the C version shows it); what cpp11's C++ frames do with the `longjmp` is the compiler's.

### 14 Smaller matters

- `printf6x.lib` is 25 byte-identical members of `rts6x.lib` plus a stub (`eh-none\pr3.obj`); toolchain.json links it
  "when printf". It adds nothing a program can reach and costs nothing; it exists for the name CCS users know.
- The build tasks run cpp11 with `-nologo` and keep its stderr unless it fails, so a warning in a runtime build is
  never shown (finding 1's cause). `RunTool Quiet="true"` does the same for shc's runtime steps.
- `RTS6X_REFERENCE=1` in grt's flags selects nothing: the `*-reference.cpp` files it guards are taken out by the
  `.sources` lists.
- The design's step 7 of start-up names `.CRT$XP*` and `.CRT$XT*` terminators; `start\sections.asm` has markers for
  `XI` and `XC` only, and `exit` runs none. No GRIDE compiler emits them, and a cl object's would be ignored.
- `msvc\shmrtd.lib`, the Debug Shalimar runtime over Microsoft's CRT, is compiled `/O2`.
- Under sim6747 and vm6747 a program's stdout reaches the host without text translation (LF), where its files go
  through the host's text mode (CR LF); the Windows builds write CR LF to both. The lines are the same.

## 6 What is good, measured

- **Identity with the UCRT**, in Release and in Debug: `stdio2.cpp` (every format line), `strto.cpp`, `misc.cpp` up to
  its last step, `files` apart from finding 11, `heap.c` apart from the uninitialised bytes a program must not read,
  `stdin1.c` (a piped CR LF input through scanf and fgets), `cxx1.cpp` (apart from addresses and the amount of memory
  the machine gave), `thr.cpp` (2,100 whole lines from seven threads, `shared=12000 atom=12000`, each thread's errno and
  thread_local its own), the Shalimar example over both runtimes.
- **Exit codes** (`ends.cpp`, 18 endings, grt Release and Debug against Microsoft's Release, Microsoft's Debug where it
  shows no dialog): `abort`, an uncaught `throw` (a class and an `int`), a pure virtual call, `std::terminate` and
  `printf("%n")` end a Release program with 0xC0000409 on both runtimes, with nothing more on stderr; the Debug runtime
  writes `abort() has been called` and ends with 3 for the first four, and a failed `assert` writes
  `Assertion failed: x == 2, file ..., line 23` first, where Microsoft's Debug CRT puts the same words in a dialog.
  `raise(SIGSEGV)` with no handler ends with 3, `signal(3, ...)` answers SIG_ERR, an integer division by zero ends with
  0xC0000094 and a runaway recursion with 0xC00000FD, on all the ways. `exit(9)` runs the `atexit` function and answers 9, `_Exit(10)` and `quick_exit(11)` run nothing and answer 10 and
  11, an access violation ends with 0xC0000005, a null `FILE *` and a null format with 0xC0000409 (the Debug runtime
  too), a normal return gives 12 - all four ways alike. (The probe's own static object, `Static st`, is destroyed on
  none of the four ways: finding 10, not the runtimes'.)
- **Imports and size**: hello world on grt imports 37 kernel32 functions (Microsoft's 76), is 97,280 bytes against
  144,896 in Release and 184,832 against 392,192 in Debug; the thread probe adds no DLL.
- **The C6747 runtime** printed the same under `rts6x.lib` and `rts6xd.lib` in every probe (`hello6`, `mem6`, `files6`,
  `cxx6b` with exceptions through four frames, rethrow, `bad_alloc`, `set_new_handler`, `vector::at`, RTTI), and
  `numbers.shl` prints the same lines under sim6747, vm6747, grt and Microsoft's CRT, Release and Debug.
- **The Release libraries are reproducible** across the two solution configurations, byte for byte, and the build's
  LibraryClosed, FrameCheck and CatalogueCheck hold.

## 7 Not checked

- Real C6747 hardware; the CCS examples; anything under gride-dbg (stepping into the runtime, the source-path map).
- The benchmarks (`bench\bench.ps1`): the machine was carrying this review's own builds and Windows Error Reporting
  stalls throughout, so no timing beyond finding 3's order-of-magnitude gap is claimed.
- UTF-8 console and paths, `wmain`/`WinMain`/`wWinMain`, a program without standard handles, files past 2 GB, `system`,
  signals from Ctrl+C: the runtime's own transcripts cover them and were not re-run.
- Microsoft's Debug CRT for the endings that open a dialog (assert, invalid parameters): those runs were killed, so
  only grt's Debug behaviour is recorded for them.
- GRIDE's engine itself (the IDE was not built): every link line was reproduced from toolchain.json by hand.
- macOS and Linux; GRIDE 2.0 and 1.0; Setup.

## 8 What was put right after the review (2026-11-05, Fable 5.1, on main at 5220ffd = cpp11 1.62)

The user asked the reviewer to sort the findings out. Main had moved to cpp11 1.62 meanwhile ("-O2 that is -O2"), which
settles two of them by itself; the rest are one commit, **84d8621** on `gride/runtimes-fixes`, pushed as `main`, built
from a clean clone of 5220ffd and checked with the review's probes, the runtime's harnesses and cpp11's conformance
suite. `docs\Runtime_Design.md`
section 16, "What the runtimes review found, put right", is the record.

| Finding | What changed | Shown by |
|---|---|---|
| 1 C6747 Release unoptimised | cpp11 1.62 optimises every file for tms6747: `strO2.cpp` now differs between -O2 and -O0 in 5,396 lines (was 0). c90 still has no C6000 optimiser; it now says so (`warning: -O2 is not yet written for tms6747 - the code is -O0's`). The build tasks log every compiler warning (`Toolchain.targets`) | `cpp11 -S p6\strO2.cpp -arch tms6747 -O2` against `-O0`; `c90 -S p6\hello6.c -arch tms6747 -O2` |
| 2 atexit list fixed at 64 / 4,096 | RTS6x's `Termination` grows by malloc'ed chunks of 64 | `atexitcap.c`: 5,000 registered, 5,000 run (was 4,095); `atexit6e.cpp` on sim6747: 65 of 65 destroyed, atexit ran (was refused) |
| 3 Debug heap ten times slower | `win\fill.asm`, `rep stosb`, in place of c90's -O0 byte loop | `bigalloc.c` on grt Debug: 100 × 10 MB 339 ms (was 11,600; Microsoft's 1,077), 1 GB 556 ms (12,624), calloc 256 MB 103 ms (3,058) |
| 4 Microsoft names absent, `_exit` undefined | `_exit` declared (`stdlib.h`, `_WIN32`) and defined (grtc), catalogue 315. The other ~180 names stay absent | `uexit.c` links on grt and exits 4 |
| 5 cpp11's driver links only Microsoft's CRT | the driver links grt by path when `lib\x86_64-windows\grtc.lib` is beside it, `d` variants under `-g` | `cpp11 p\hello.cpp -o hello.exe` with LIB empty: builds, runs, KERNEL32.dll alone (was `libcmt.lib: cannot open`) |
| 6 no CodeView in Release; shmrtd without -g | `shmrtd.lib` over grt and `shmrt-debug` get `-g`; `msvc\shmrtd.lib` `/Od /Z7`. Release CodeView left (cpp11 writes different code under -g) | shc.vcxproj, grt.vcxproj |
| 7 C6747 16 KB stack, 1 MB heap | 1 MB stack and 16 MB heap in both toolchain.json files, GRIDE 1.0's `compile.cpp` and the demo | `mem6.c -DSTACK=1`: `deep(200)` = 1412 (was an undefined instruction); 64 × 16,000 bytes no longer fill the heap |
| 8 Debug-configuration libraries differ | **withdrawn as a defect**: the four bytes are the object's own path in masm's S_OBJNAME record (`build\Debug\obj` against `build\Release\obj`); code and data identical, paths alone differ | `REVIEW\vol\mc-masm-*.obj`, the `.debug$S` payloads compared |
| 9 `map<string,int>::operator[]` on tms6747 | fixed in cpp11 1.62 | `map6.cpp` links and prints 1 |
| 10 trivially-constructed static never destroyed | cpp11 registers the destructor when no initialiser follows (`ParserTopLevel.cpp`, `ParserStmt.cpp`, `buildStaticConstruction`) | `trivdtor.cpp`: both destructor lines, grt, Microsoft's and sim6747 |
| 11 `ftell` after `ungetc` at 0 | `grt::tell` answers 0 | `files3.c`: grt = Microsoft's |
| 12 `fopen` leniency | `"r+b+"` and `"rbt"` refused as the UCRT refuses them; NULL + EINVAL for an invalid mode kept and recorded as a deviation. RTS6x's `malloc` sets ENOMEM | `badmode.c`: `r+b+` NULL errno 22 |
| 13 longjmp from a SIGSEGV handler hangs (C++) | cpp11's x86-64 generator evaluates an operand that calls `setjmp` first, nothing parked (c90's rule, `X86_64Linux.cpp`) | `sigjmp.cpp`: runs to `done` on all four ways |
| 14 `RTS6X_REFERENCE` | dropped from grt's flags | grt.vcxproj |
| 15 (found once 13 let `misc.cpp` run to its end) `strftime`'s `%c` zero-pads the day on Windows | `%c` is `%a %b %e %H:%M:%S %Y` on every target, as the UCRT writes it ("Thu Jan  4 12:00:00 2024"); the Windows branch had `%d` from an earlier reading | `misc.cpp` line 4, grt against Microsoft's |
| 16 `gmtime(-1)` answers null | the time functions take the UCRT's lower bound, 12 hours before 1970 (`_MIN_LOCAL_TIME`); the weekday stays the right one (the UCRT calls 1969-12-31 a Thursday) | `misc.cpp` line 8 |
| 17 `fmod(x, 0)` answers `-nan(ind)` | the positive quiet NaN, as the UCRT answers it; an infinite x keeps the negative one | `misc.cpp` line 14 |

The first build with the tasks' warnings on showed what had been hidden: cl's "overriding '/O2' with '/Od'" on the
Debug Shalimar runtime over Microsoft's CRT (the line carried both; `/O2` is the Release line's alone now), and cpp11
1.62's "'x' is read here and nothing has been assigned to it" on five RTS6x files, each a variable the call before it
fills through a reference (`if (special(x, result)) return result;`): the code is right, the warning does not count an
out-parameter as an assignment - cpp11's to refine.

Two more lines of `misc.cpp` differ and are the probe's own, not the runtime's: `strerror` and `setlocale` answer a
pointer to one buffer on the UCRT, so four calls in one `printf` show the last answer four times, where grt answers
four constant strings; and the UCRT accepts `setlocale(LC_ALL, "de_DE")`, which the design refuses on purpose (6, "no
languages"), so `localeconv` then shows a German decimal comma there.

**The checks on the rebuilt toolchain** (a clean rebuild of the clone at 5220ffd with every change in, Release):
- the review's probes again: section 6's identities hold, every difference above is gone; `misc.cpp` now differs from
  Microsoft's run only in its own two lines and the clock's spin count;
- the runtime's harnesses, `-Bin` at the clone: completeness 304 of 304 names and 4 of 4 entries; the UCRT probes
  (`names`, `quickexit`, `exitnow`, `divide`, `rethrow`, `perthread`, `printf`, `sort`, `strings`, `wide`, `signals`,
  `badsignal`, `default`, `fault`, `divide`) at -O2, -O0 and on the debug libraries, each the same as its transcript on
  both runtimes, except `exitnow` on the debug libraries on Microsoft's run alone, the recorded difference; threads 10 of
  10; filing; debug; start (36 of 36 as link.exe); math99 at -O2 and -O0, every line; utf8 and utf8 on the debug
  libraries; Shalimar's access tests 76 of 76 over the release runtimes and 76 of 76 over the debug ones, twice (before
  and after the Shalimar runtime's rebuild); the toolchain demo, Release and Debug, all eleven programs;
- cpp11's conformance suite on the rebuilt compiler: 1,982 cases, **1,091 of 1,091 valid cases pass at -O0 and at
  -O2, 889 of 889 invalid ones refused, no regression** against 1.62's baseline. The first run called two cases
  `differs` at both levels with their bytes equal to cl's: the suite's two longest file names, whose `.expected` paths
  reach 260 characters under the clone's folder, which .NET Framework's `ReadAllText` in the gate refuses; from the
  release worktree's copy of the suite (179 characters) both pass with the same compiler;
- the Debug Shalimar runtimes carry lines now: `shmrtd.lib` over grt 40 of 40 members with a CodeView lines
  subsection (was 0), `msvc\shmrtd.lib` 41 of 41 (was 0), `shmrt6xd.lib` DWARF in 39 of 40 (the 40th is the C6747's
  empty `shm_c_system`);
- the C6747 probes under sim6747 over `rts6x.lib` and `rts6xd.lib` print the same, with `errno` 12 on a failed `malloc`.

Not done here: the solution's Debug configuration was not rebuilt after the changes (it builds the same libraries with
the same project files); cpp11's `Version.h` stays 1.62, for its lane to bump at the next release; the C6000
optimiser for c90, the Microsoft names beyond `_exit`, and Release CodeView remain open. The clone's incremental builds
under %TEMP% (MSBuild's MSB8029) once produced a cpp11.exe that misread its command line; every figure above is from
clean rebuilds, and a cpp11 built without the size-reducing link settings behaves the same as one built with them, so
those settings are not behind anything in this review.

## 9 Where the material is

`REVIEW` = `C:\Users\GRA\AppData\Local\Temp\claude\D--GRA-My-Documents-GIT-Test\60fa293a-82ab-44be-a4ab-1b78d2445764\scratchpad`:
`g\` the clean clone with both configurations built (`g\GRIDEd\x64\Release`, `g\GRIDEd\x64\Debug`), `build-Release.log`
and `build-Debug.log`; `w.ps1`, `c6.ps1`, `sh.ps1` the runners; `p\`, `p6\` the sources; `out\<name>\<way>.out|.err|.result`,
`out6\`, `outs\` the runs; `out\ends\<ending>-<way>.out` the exit-code runs; `vol\` the `-O2`/`-O0` comparisons and the
two masm objects.
