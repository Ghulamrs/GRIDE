# GRIDE's runtime for Windows - design

Draft 3, 2026-10-23. Status: **accepted**: the go-ahead was given on 2026-10-23, with the decisions of
section 17 (the review record is section 18). **Stage 1 is done** (2026-10-23; section 16, "Stage 1 as built"); stage 2 is next. A prototype exists (branch
`grt-prototype`, c88573b, not merged). What it measured is in section 15; it is stage 1's starting point,
reorganised into the libraries of section 4.

Contents: 1 Goals and limits - 2 What the compilers' code needs - 3 The components - 4 The libraries -
5 Headers and libraries on disk - 6 The Windows layer - 7 Start-up and exit - 8 C++ support -
9 Machine-level contracts - 10 Efficiency and the benchmarks - 11 Correctness - 12 Build -
13 Packaging - 14 Shalimar's direct access - 15 What the prototype found - 16 The stages -
17 Decisions asked of the reviewer - 18 Review record.

---

## 1 Goals and limits

**Goal.** A Windows program that GRIDE builds links and runs on any machine with Windows and GRIDE's
installation, without Visual Studio and without the Windows SDK. The program imports `kernel32.dll` alone
(which reaches `ntdll.dll` itself).

**Built by our toolchain only.** c90 compiles the runtime's C, cpp11 1.30 its C++, GRIDE's masm its
assembly, and GRIDE's link (`/LIB`) its archives and import libraries. cl, ml64, lib.exe and Microsoft's
link.exe never build it.

**Efficiency is paramount, correctness is the gate.**
- A program built against GRIDE's runtime prints exactly what the same objects print when linked with
  Microsoft's CRT: stdout and stderr byte for byte, the same exit code.
- On top of that, it must be at least as fast as Microsoft's static CRT on the benchmark set (section 10.5),
  and its executables smaller.

**Microsoft's sources are inspiration only.** vcruntime (`VC\Tools\MSVC\14.44.35207\crt\src\vcruntime`)
and the UCRT (`Windows Kits\10\Source\10.0.26100.0\ucrt`) are read for sequences, data layouts and edge
cases: the ABI tables cpp11 emits, the initialiser order, what printf prints for a NaN, what `abort`
returns. No text of theirs is copied into GRIDE: GRIDE ships this runtime, and their licence does not allow
it. RTS6x stays the base of the portable C library.

**Limits.**
- x86_64-windows only. RTS6x's C6747 code is unchanged by Windows work, except in stdio, which Windows gets
  as its own files (section 10.3).
- Console programs (`main`) first. `wmain`, `WinMain`, UTF-8 and DLLs come later (section 16).
- The "C" locale only, with narrow characters in the ANSI code page, as the Microsoft CRT's "C" locale has
  them.

---

## 2 What the compilers' code needs

The prototype compiled every C and C++ example for x86_64-windows with c90 and cpp11 (-O2, and -O0 -g) and
listed each object's undefined symbols. It listed shalimar's output and runtime the same way, and the
reviewer read the compilers' code generators. The names below are the runtime's fixed contract. Their names,
layouts and behaviour are the Microsoft ABI's, because both compilers emit Microsoft-ABI code and the same
objects must also link with Microsoft's CRT (section 13.2).

| Name | Emitted by | What it must do |
|---|---|---|
| `mainCRTStartup` | link (the default entry of a console program with `main`) | start-up and exit, section 7 |
| `__chkstk` | both compilers, for a frame of a page or more, as cl (c90 since 04ce087; before, its prologue was a plain `sub rsp, n`) | touch each page from rsp down by rax bytes, keep every register but r10, r11 and the flags |
| `_fltused` | cl's objects (a prebuilt library's, section 13.4) | a data word |
| `memset` | cpp11 (value-initialisation); neither compiler calls `memcpy`/`memmove` implicitly (c90 copies aggregates inline) | section 10.2 |
| `.CRT$XCU` entries | cpp11 (dynamic initialisers) | called in order before `main` |
| `.CRT$XIU` entries | (C initialisers, int-returning) | called before the C++ ones; a non-zero answer ends start-up |
| `atexit` | cpp11 (each static object with a destructor) | run in reverse order by `exit` |
| `_Init_thread_header`, `_Init_thread_footer`, `_Init_thread_abort` | cpp11 (function-local statics; it calls the header every time) | the guard protocol of section 8.2 |
| `thread_local`: `_tls_index` (the gs:[88] walk), `__tls_guard`, `__dyn_tls_on_demand_init`, `.CRT$XDU` entries with `/include:__dyn_tls_init`, `__tlregdtor` | cpp11 | the TLS directory (`_tls_used`, the `.tls` markers, the `.CRT$XLA`..`XLZ` callbacks), the per-thread dynamic initialisers, thread-local destructors; section 9.6 |
| `??2@YAPEAX_K@Z`, `??3@YAXPEAX@Z`, `??_U...`, `??_V...`, the nothrow forms, `?nothrow@std@@3Unothrow_t@1@B` | cpp11 | `operator new`/`delete`, array and nothrow |
| `_purecall` | cpp11 (pure virtual slots) | Microsoft's behaviour: section 4.3 |
| `??_7type_info@@6B@` | cpp11 (every type descriptor) | type_info's vftable |
| `__RTtypeid`, `__RTDynamicCast`, `__RTCastToVoid` | cpp11 | section 8.3 |
| `__std_type_info_compare`, `__std_type_info_hash`, `__std_type_info_name`, `?__type_info_root_node@@3U__type_info_node@@A` | cpp11's `<typeinfo>` | as vcruntime's |
| `__std_exception_copy`, `__std_exception_destroy` | cpp11's `<exception>` | std::exception's message |
| `__CxxFrameHandler3`, `_CxxThrowException` | cpp11 (any function with a destructor to run, any throw) | section 8.4 |
| `abort` | cpp11 (a failed `thread_local` registration and the like) | section 4.3 |
| `__acrt_iob_func`, `_errno` | the headers (`stdout`, `errno`) | the UCRT's spellings, section 5.2 |
| `_setjmp` (two arguments), `longjmp` | the headers | Microsoft's x64 jump buffer |
| `_assert` | the headers (`assert`) | "Assertion failed: %s, file %s, line %d", then `abort` |
| `_time64`, `_localtime64`, `_gmtime64`, `_mktime64`, `_ctime64`, `_difftime64` | cpp11's `<time.h>` | 64-bit `time_t` |
| `strtoll`, `strtoull`, `strtof`, `strtold`, `system` | cpp11's `<stdlib.h>`; `<string>` calls `strtoll`/`strtold` | the general-use component |
| `_fileno`, `fileno`, `_setmode`, `_access`, `_getcwd`, `_chdir`, `_mkdir`, `_rmdir`, `_stat64i32`, `_findfirst64i32`, `_findnext64i32`, `_findclose` | cpp11's `io.h`, `direct.h`, `sys/stat.h` | the filing component |
| the kernel32 functions cpp11's `windows.h` declares | user code | in the import library's `.def` |
| `shm_*` | shalimar | Shalimar's runtime, layered on this one (section 14) |
| `hypot`, `log2`, `cbrt`, `fabs`, `pow`, `fmod`, `trunc`, `round`, ... | shalimar's built-ins (`Builtin.cpp`) | the math component; all must exist before Shalimar programs are an exit test |
| the C library | user code | the components of section 3 |

**What the compilers must fix first.** These fixes are made in this work as stage 1's first steps (decision 9).
Each is a commit of its own, under that compiler's own gates. **All were made on 2026-10-23:**
- cpp11 1.30: 15d6b01, 7cfd746, 55a1ea7, cc46297;
- c90: 04ce087 (the probe), 7d762e9 and 35c2879 (setjmp), 0882362 and 9f1ae45 (two more, found building the runtime);
- GRIDE's masm: 28f08d2 (section 9.2's instructions), a896f36 (OPTION NOKEYWORD).

The fixes:
- cpp11 1.29, on every 64-bit target, computes `pointer - unsigned int` as `p + (2^32 - n)`. The index is
  negated in 32 bits before it is widened (`parser\ParserExpr.cpp`, `arithmetic()`).
- cpp11's MASM spelling renames a function it defines whose name is a MASM keyword (`backend\Masm.cpp`'s
  list): `fabs` and C90's `div` among them.
- cpp11 `-g -masm=masm`: a file with a dynamic initialiser references labels it never defines
  (`$LNend$_GLOBAL__sub_I_<file>`). This blocks building the runtime with CodeView (section 9.3).
- c90 has no stack probe; it gets `__chkstk` for frames of a page or more, as cl has. Until then a runtime
  frame compiled by c90 must stay under 4 KB. The build keeps checking that afterwards, as a guard
  (section 12.3).
- c90 at -O0: `if ((n = setjmp(jb)) == 0) longjmp(jb, 7);` never returns, on Microsoft's CRT as well.

---

## 3 The components

The runtime is organised in the components below. Each owns its headers (the C `.h` and the C++ `<c...>`
form), a group of objects in the libraries of section 4, a part of the Windows layer, an efficiency plan, a
benchmark and a correctness test. This table drives the header layout (section 5) and the libraries
(section 4).

| Component | Headers | Objects / library | Windows functions | Stage | Benchmark | Test |
|---|---|---|---|---|---|---|
| **math** | `math.h`/`<cmath>`, `float.h`/`<cfloat>`, `limits.h`/`<climits>`; later `<complex>` | `grtc`: RTS6x's math (double-double kernels); `sqrt`, `fabs`, `floor`, `ceil`, `trunc`, `round` in MASM; C99's `hypot`, `log2`, `cbrt`, `round`, `lround`, `fmin`/`fmax` (new) | none | baseline: every name the examples and shalimar emit; widening: the rest of C99's, faster kernels | sqrt/sin/exp/log/pow throughput | test vectors against the UCRT, in ulps; errno as the UCRT sets it |
| **string** | `string.h`/`<cstring>`, `ctype.h`/`<cctype>`; later `wchar.h`, `wctype.h` | `grtc`: str*/mem* search in MASM (SSE2) where hot, RTS6x's C++ for the rest; `strcoll`, `strxfrm` ("C" locale: `strcmp`, copy); ctype tables | none | baseline; SSE2 in widening | strlen/strcmp/strchr/memchr across sizes | probe against the UCRT; page-boundary tests (a string ending at the last byte of a committed page) |
| **memory** | `stdlib.h` (malloc ...), `memory.h`, `<new>` | `grtc`: the allocator front end, mem* in MASM; `grtwin`: the allocator's region back end; `grtcxx`: `operator new`/`delete`, the new-handler | `VirtualAlloc`/`VirtualFree`, `HeapAlloc`/`HeapFree` (large blocks) | baseline: mem*, malloc over the process heap; widening: the allocator | memcpy/memset/memmove 16 B to 1 MB; malloc/free churn | heap stress (random sizes, realloc, calloc zeroing, alignment 16); Debug fill bytes |
| **filing** | `stdio.h` (fopen ... tmpfile, tmpnam, fgetpos/fsetpos), `io.h`, `fcntl.h`, `sys/stat.h`, `sys/types.h`, `direct.h` | `grtwin`: descriptors (512 FILEs, 8,192 descriptors as Microsoft's), `_open`/`_read`/`_write`/`_lseek[i64]`/`_close`, `_stat64`, `_access`, `_findfirst`, `_getcwd`/`_chdir`/`_mkdir`/`_rmdir`; `_fseeki64`/`_ftelli64` | `CreateFileW`, `ReadFile`, `WriteFile`, `SetFilePointerEx`, `CloseHandle`, `GetFileType`, `DeleteFileW`, `MoveFileExW`, `GetFileAttributesExW`, `FindFirstFileW`/`FindNextFileW`/`FindClose`, `GetCurrentDirectoryW`, `SetCurrentDirectoryW`, `CreateDirectoryW`, `RemoveDirectoryW`, `GetTempPath2W`, `MultiByteToWideChar` | baseline: stdio's files; widening: the io.h/direct.h/stat layer, files past 2 GB | fwrite/fread and fgets of a large file | file probe (text and binary, append, seek/tell, CR LF, a 3 GB file), against the UCRT |
| **general use** | `stdlib.h`/`<cstdlib>` (rand, qsort, bsearch, strtol/strtoll, strtod/strtof, atoi, abs, div/ldiv, getenv, system, exit, atexit, at_quick_exit, abort), `time.h`/`<ctime>` | `grtc`: RTS6x's stdlib, Calendar and TimeFormatter; `grtwin`: the clock, the time zone (`TZ` first, then Windows' rule applied as the UCRT applies it), the environment, `system` | `GetSystemTimeAsFileTime`, `QueryPerformanceCounter`/`Frequency`, `GetTimeZoneInformation`, `GetEnvironmentStringsW`, `CreateProcessW`, `WaitForSingleObject`, `GetExitCodeProcess` | baseline: what the examples call; widening: the rest | qsort; strtod; `clock()` resolution | probes against the UCRT (rand's sequence is Microsoft's) |
| **standard I/O** | `stdio.h`/`<cstdio>` (printf/scanf families, stdin/stdout/stderr, setvbuf/setbuf), `stdarg.h`; the C++ streams `<iostream>`, `<fstream>`, `<sstream>`, `<iomanip>` (cpp11's library, on top) | `grtc`: RTS6x's Formatter, Scanner and FloatText (shared with the C6747); grt's own Windows stream layer (section 10.3) | `WriteFile`, `ReadFile`, `GetConsoleMode`, `GetStdHandle` | baseline: the stream layer and its buffering; widening: printf/scanf fast paths, `%zu`/`%td`/`%I64d` | printf of ints, floats, strings to a file and to NUL (1M calls); scanf of ints and floats | 1,838-case float printf/scanf vectors, NaN parsing, the example sets |
| **the rest of the headers** | `assert.h`, `errno.h`, `stddef.h`, `stdarg.h`, `setjmp.h`, `signal.h`, `locale.h`, `stdint.h`, `<new>`, `<typeinfo>`, `<exception>` | `grtc` (setjmp in MASM, signal, locale "C"); `grtwin` (errno storage, the structured-exception filter and Ctrl+C for signal); `grtcxx` (RTTI, statics, thread_local, exceptions) | `SetConsoleCtrlHandler`, `SetUnhandledExceptionFilter`, `ExitProcess` | baseline; exceptions in widening | - | probes; a SIGSEGV/SIGFPE/SIGINT probe against the UCRT |

---

## 4 The libraries

### 4.1 Static libraries, not a DLL

**Decision: static libraries.** No stage below builds a DLL; the design keeps that possible (section 4.4).

| | Static libraries | A runtime DLL beside the program |
|---|---|---|
| Size per program | only what the program reaches (the prototype's hello: 70 KB against Microsoft's static 143 KB) | a small .exe, plus the DLL (hundreds of KB) shipped beside every program or on PATH |
| Speed | direct calls; no loader work for the runtime | every call through an import thunk (our compilers do not emit `dllimport`); the DLL's own start-up |
| Deployment | the .exe alone runs anywhere with Windows | the DLL must travel with the program, and match its version |
| Versioning | none needed: each program carries its runtime | a versioned name (`grt1.dll`) and an ABI kept forever |
| Several modules sharing state (heap, FILE, errno) | not possible across DLLs (the same limit as `/MT`) | possible |

GRIDE's programs are single-module console programs, so the last row does not arise yet; every other row
favours static libraries.

### 4.2 The split, and its layers

Each library is a layer that calls only itself and the layers below it. A library is pulled in only for
what a program reaches. The layers, top to bottom:

| Library | What is in it | Calls |
|---|---|---|
| `grtstart` | the root: `mainCRTStartup` (later `wmainCRTStartup`, `WinMainCRTStartup`), the `.CRT$X*` markers, the TLS directory (`_tls_used`, `.tls` markers, the callback array), `__chkstk`, `_fltused`, `main`'s structured-exception filter, `__setusermatherr` | everything below, and the program's `main` |
| `grtcompat` | legacy and Microsoft-compatibility names: the 32-bit time functions old c90 objects call (`time`, `localtime` with a 32-bit `time_t`), POSIX names Microsoft keeps in `oldnames.lib` (`fileno`, `close`, ...), what cl-built objects need (`/GS`'s `__security_cookie`, `__security_check_cookie`, `__GSHandlerCheck`, `__report_rangecheckfailure`; later `__CxxFrameHandler4`) | `grtcxx`, `grtc`, `grtwin` |
| `grtcxx` | C++ support: `operator new`/`delete`, the new-handler, `_purecall`, type_info and RTTI, function-local statics, thread_local's dynamic initialisation and destructors, `std::exception`'s helpers, exceptions | `grtc`, `grtwin` |
| `grtc` | the ISO C library: C90 core and the C99/C11 additions (math, string, memory's front end, the formatting and scanning engines, stdlib, time's calendar, locale, signal, setjmp, assert), and the Windows stream layer | `grtwin` |
| `grtwin` | the Windows layer, the lowest: descriptors, the allocator's back end, errno's storage, the environment, argv, the clock and the time zone, the filing component's io.h/direct.h/stat functions, `CreateThread`'s wrapper (section 9.6), `system`, the process's end (`ExitProcess`, the fast-fail) | `kernel32` only |
| `kernel32` | the import library, generated from `kernel32.def` | - |
| (`shmrt`) | Shalimar's runtime, above all of these (section 14) | all |

**Link order, fixed:** the program's objects, the project's libraries, `shmrt` (a Shalimar program),
`grtstart`, `grtcompat`, `grtcxx`, `grtc`, `grtwin`, `kernel32`. GRIDE's link searches the libraries again
until nothing more resolves, so the order decides nothing as long as no name is defined twice. That is what
the build checks (section 12.3): **every name defined once across the runtime's libraries**, and each
library's undefined names defined in itself or below it.

**Why several and not one `grt.lib`** (the prototype's): the layering can be checked; the legacy names stay
out of a link that does not want them; the link line shows which layer a name came from; and `grtwin` is
the only importer of kernel32.

### 4.3 Release and Debug variants

| Variant | Names | Compiled | CodeView | Failures (Microsoft's behaviour, read from the UCRT's `abort.cpp` and `invalid_parameter.cpp`) |
|---|---|---|---|---|
| Release | `grtc.lib`, `grtcxx.lib`, ... | -O2 | yes, once cpp11's `-g` bug is fixed | `abort`, a failed `assert`, an uncaught exception and `_purecall` end the program through the fast-fail (`int 29h`, FAST_FAIL_FATAL_APP_EXIT): exit 0xC0000409. The commonest invalid parameters Microsoft checks (a null `FILE *` or format) fast-fail the same way (FAST_FAIL_INVALID_ARG) |
| Debug | `grtcd.lib`, `grtcxxd.lib`, ... (a `d`, as libcmt/libcmtd) | -O0 | yes | `abort` writes "abort() has been called" to stderr and ends with `_exit(3)`. Microsoft's Debug runtime shows a dialog first; GRIDE's writes the same words without one (a recorded deviation: a dialog would stop a program run in GRIDE's Output pane). The allocator keeps headers and guards and fills as Microsoft's debug heap does (0xCD new, 0xDD freed, 0xFD guard), so a program reading freed or new memory prints what it would there |

A Debug build of a program links the Debug variants. Both variants keep the same ABI, and the same output
for a correct program.

### 4.4 A DLL later

Every public function has a plain C name and a catalogue entry (section 14.2). The compilers' code reaches
no runtime data directly, except the thread-local storage the TLS directory describes (section 9.5). So a
`grt1.dll` whose `.def` is generated from the catalogue is possible without changing a header. It is not
planned.

---

## 5 Headers and libraries on disk

### 5.1 Today

- `lib\` is two things: c90's header folder (toolchain.json `installIncludeDirs: ["lib"]`, the 16 C90
  headers) and the runtime library folder (`shmrt-x86_64-windows[-debug].lib`, `rts6x-tms6747\`,
  `shmrt-tms6747[-debug]\`, and `ccs\` with CCS's option tables).
- `include\` is cpp11's header folder. It holds its C++ headers (C++03 and C++11 together), a second,
  different copy of the 16 C headers plus `stdint.h`, and the Windows/POSIX ones (`windows.h`, `io.h`,
  `direct.h`, `fcntl.h`, `sys\`).
- The two C copies differ in every file. cpp11's are `extern "C"`, add `snprintf`, `_fileno` and
  `strtoll`, and on Windows give `time_t` 64 bits behind `_time64`; c90's `time_t` is a 32-bit `long`. The
  UCRT exports no plain `time`, `localtime`, `mktime`, `ctime`, `gmtime` or `difftime` (they are inline in
  its headers), so a c90 program calling `time()` does not link with Microsoft's CRT today.
- In the source trees, c90's headers live in `Compiler-Ci\lib`, cpp11's C headers in `C++Optimize\lib` and
  its C++ headers in `C++Optimize\include`. Each project copies them into `x64\<Config>\lib` or `\include`.
  The compilers also have their source tree's folder compiled in (`CC1_INCLUDE_DIR`, `CXX1_INCLUDE_DIR`,
  `CXX1_CXX_INCLUDE_DIR`).

### 5.2 One set of C headers

**One C header set, shared by c90 and cpp11**, the single source of truth for what the runtime declares
(checked against the catalogue, section 14.2). Each header:
- declares C90 first: everything C90 declares, unconditionally;
- adds C99/C11 under `#if defined(__cplusplus) || defined(__STDC_VERSION__) || defined(_GRIDE_C99)`. c90 sees
  C90 unless a project asks for more (`_GRIDE_C99`); cpp11 sees the C99 library C++11 includes;
- wraps itself in `extern "C"` for C++;
- keeps the target branches it has today (`_WIN32`, `__APPLE__`, `__linux__`, `__TMS320C6X__`);
- names, on Windows, what objects can link with either runtime: the UCRT's exports and the functions
  `legacy_stdio_definitions.lib` gives Microsoft's link (printf, sprintf, the scanf and v- families), with
  the UCRT's spellings (`__acrt_iob_func`, `_errno`, `_setjmp` with its frame argument, `_time64` ...)
  (section 13.2).

Where the two copies disagree today, the merged header takes:
- `time_t`: 64 bits on Windows for both compilers, through `_time64` and its siblings. c90 has no `inline`,
  so the plain names are macros there (`#define time(t) _time64(t)`), as the UCRT's own header makes them
  inline functions. Old c90 objects calling the 32-bit `time` keep linking, because `grtcompat` defines it.
- `snprintf`, `vsnprintf`, `strtoll`, `long long` conversions: C99, so behind the guard for c90.
- `_fileno`, `_setmode`: Windows-only and Microsoft's, behind `_WIN32`; `fileno` behind a POSIX-names guard.

**The C++ headers by level too:** each of cpp11's headers keeps its C++03 part first and its C++11 additions
under `#if __cplusplus >= 201103L`. The catalogue records each name's level (C90, C99, C11, C++03, C++11,
Microsoft, POSIX, GRIDE).

### 5.3 The layout

```
<GRIDE>\include\
    c\              the C headers, C90 then C99/C11 additions in each (assert.h ... time.h, stdint.h,
                    stdbool.h, inttypes.h)
    c\sys\          sys/types.h, sys/stat.h
    c++\            cpp11's headers, C++03 then C++11 in each (iostream, map, vector, ... array,
                    initializer_list, type_traits), and the <cxxx> wrappers over c\
    win\            Windows and POSIX compatibility: windows.h, io.h, direct.h, fcntl.h
<GRIDE>\lib\
    x86_64-windows\ grtstart.lib grtcompat.lib grtcxx.lib grtc.lib grtwin.lib kernel32.lib (Release)
                    grtstartd.lib grtcompatd.lib grtcxxd.lib grtcd.lib grtwind.lib (Debug)
                    shmrt.lib shmrtd.lib (Shalimar over GRIDE's runtime)
                    msvc\shmrt.lib, msvc\shmrtd.lib (Shalimar over Microsoft's CRT, built by cl)
    tms6747\        rts6x.lib rts6xd.lib printf6x.lib printf6xd.lib shmrt6x.lib shmrt6xd.lib
                    shmrt\ shmrt-debug\ (the Shalimar runtime's assembly the emulator takes)
<GRIDE>\share\ccs\  CCS's option tables (today lib\ccs: data, neither header nor library)
```

A compiler is given its include folders in this order: cpp11 gets `include\c++`, `include\c`,
`include\win`; c90 gets `include\c`, `include\win`.

### 5.4 Migration (stage 3)

| What | Change | Kept working by |
|---|---|---|
| toolchain.json `installIncludeDirs` | c90 `["include\\c", "include\\win"]`, cpp11 `["include\\c++", "include\\c", "include\\win"]` | GRIDE passes them; projects never name them |
| the compilers' own search (no -I) | c90 and cpp11 look in `<exe>\include\c` ... before their compiled-in source path | a small change in each `Driver.cpp`, by the compiler's owner; until it lands, Setup also installs the old `lib\*.h` and `include\*` |
| library entries | `directory` becomes `lib\\x86_64-windows` or `lib\\tms6747`, and `files` the new names | toolchain.json alone: GRIDE's lookup reads the entries |
| RTS6x's and shalimar's builds | write into `lib\tms6747\` | `rts6x.vcxproj`, `shc.vcxproj` output paths |
| the emulator's Shalimar runtime | `assemblyDirectories` `lib\\tms6747\\shmrt[-debug]` | toolchain.json; `SHMRT6747[D]` still win |
| CCS tables | `ccs.definitionsDir` `share\\ccs` | toolchain.json |
| Setup | installs the new tree (and, for one release, the old header folders) | build-setup.ps1 from x64\Release; test-setup.ps1 counts per folder |
| the check | both example sets (Debug, Release), the C6747 examples on sim6747 and vm6747, the toolchain demo, before and after | section 11 |

---

## 6 The Windows layer (`grtwin`)

- **Descriptors.** RTS6x's numbering (0, 1, 2 the standard handles, 3 up the files), with Microsoft's limits
  (8,192 descriptors). Each descriptor has a handle, `_O_TEXT`/`_O_BINARY`, its file type, an append flag
  and a 64-bit position. Text mode writes LF as CR LF and reads CR LF as LF, as the Microsoft runtime does.
  A CR at the end of a read looks past itself for an LF. CTRL+Z ends the input **only on a file or a pipe**;
  on a character device it is passed through (the UCRT's `lowio\read.cpp`).
- **What the stdio layers above use.** RTS6x's `CioChannel` interface (open, close, read, write, seek,
  unlink, rename, environment, time, clock) is kept as the boundary, implemented over Win32. Positions are
  64-bit. Unbuffered: buffering is the stream layer's (section 10.3).
- **Handles** are inheritable unless `_O_NOINHERIT` is given, as the UCRT's are, so `system()` and
  `CreateProcess` children see what a Microsoft-built program's would.
- **Paths.** The ANSI code page is converted to UTF-16 (`MultiByteToWideChar`) and the `W` functions are
  called, as the UCRT does in the "C" locale. UTF-8 is a later stage: a manifest's `activeCodePage` needs
  resources, which GRIDE's link does not write yet (it accepts and ignores `/manifest`).
- **errno** comes from Windows' error numbers by the Microsoft runtime's mapping (written from its
  documented values).
- **The environment** is read once, narrow, with the `=C:` entries left out. `getenv` is a case-insensitive
  search, and its pointers stay valid for the program's life.
- **argv** comes from `GetCommandLineW` by Microsoft's rules. `argv[0]` is taken by quotes alone, with no
  backslash rule; the arguments take the rules for 2n backslashes before a quote and `""` inside quotes.
  Each argument is narrowed to the ANSI code page.
- **Time.** `GetSystemTimeAsFileTime` gives the time. `clock()` counts over `QueryPerformanceCounter` from
  start-up, in CLOCKS_PER_SEC (1000) units as Microsoft's. Local time follows the UCRT's algorithm, not
  `SystemTimeToTzSpecificLocalTime`: `TZ` first, then `GetTimeZoneInformation`'s bias and transition
  dates, applied by the same rule for every year.
- **`tmpfile`** opens a unique name in `GetTempPath2W`'s folder with `FILE_FLAG_DELETE_ON_CLOSE`, so two
  programs never collide and a crash leaves nothing. `tmpnam` gives names in the same folder.
- **Console:** a handle is a console when `GetConsoleMode` succeeds. NUL is a character device but no
  console.

---

## 7 Start-up and exit (`grtstart`)

The sequence, modelled on vcruntime's `exe_common.inl` (read for the order, not copied):

1. `mainCRTStartup`: the CPU's features (CPUID once: SSE4.1, ERMSB, AVX2 - section 10.2), the clock's
   origin, the standard descriptors. MXCSR and the x87 control word are left as Windows sets them, as
   Microsoft's x64 CRT leaves them.
2. The C initialisers, `.CRT$XIA` .. `.CRT$XIZ`, null entries skipped. A non-zero answer ends the program
   with 255 and a message.
3. `__setusermatherr`, when the program defines `_matherr`.
4. The C++ initialisers, `.CRT$XCA` .. `.CRT$XCZ`. Each registers its object's destructor with `atexit`.
5. The main thread's `thread_local` initialisers. Windows calls the TLS callbacks (`.CRT$XL*`) before the
   entry point, and they run `.CRT$XDA` .. `.CRT$XDZ` for the thread, as `__dyn_tls_init` does.
6. argv and envp, then `exit(main(argc, argv, envp))`, with `main` called inside a structured-exception
   filter. An access violation, an illegal instruction or a floating-point fault is delivered to the
   program's `signal` handler for SIGSEGV, SIGILL or SIGFPE when it has one, as Microsoft's
   `_seh_filter_exe` does; otherwise it goes on to Windows. Ctrl+C reaches SIGINT through
   `SetConsoleCtrlHandler`, on the thread Windows starts for it.
7. `exit`: the `atexit` list, last registered first (one list for C's functions and C++'s destructors, as
   Microsoft keeps it), then the `.CRT$XP*` pre-terminators and `.CRT$XT*` terminators, then every stream
   flushed and closed, then `ExitProcess`. `quick_exit` runs the `at_quick_exit` list and nothing else.
8. `abort`, `_exit`, `_Exit`: section 4.3; no `atexit` and no flush.

GRIDE's link orders an output section's contributions by the name after `$`. So the markers in
`.CRT$XCA`/`.CRT$XCZ` bracket every object's `.CRT$XCU`; the prototype verified this.

---

## 8 C++ support (`grtcxx`)

### 8.1 Allocation
RTS6x's `Allocation` (the new-handler loop) under Microsoft's mangled names: `operator new`/`delete`, the
array and nothrow forms, and sized `delete`, which cl's objects use. The placement forms are inline in
`<new>`. A failed allocation throws `std::bad_alloc`.

### 8.2 Function-local statics and thread_local
- **Function-local statics.** cpp11 calls `_Init_thread_header(&guard)` every time and constructs when the
  guard then reads -1. `_Init_thread_footer` marks the static made; `_Init_thread_abort` (a constructor
  that threw) marks it unmade. The guard is an int: 0 not made, -1 being made, anything else made. A second
  thread waits on a condition variable while the first constructs (C++11 6.7/4). The lock is taken only once
  the program has a second thread (section 9.6).
- **thread_local.** cpp11's `.CRT$XDU` initialisers run once per thread from the TLS callback
  (`__dyn_tls_init`). `__dyn_tls_on_demand_init` is their on-demand form behind `__tls_guard`.
  `__tlregdtor` registers a thread-local destructor, which runs when its thread ends (the callback's
  DLL_THREAD_DETACH) or at `exit` for the main thread.

### 8.3 RTTI, in Microsoft's x64 layout
- The complete object locator before each vftable (signature 1, image-relative offsets, the image base from
  the locator's own offset); the type descriptor (`??_R0`: type_info's vftable, the spare word, the
  decorated name); the class hierarchy (`??_R3`) and the base class array (`??_R2`, `??_R1` entries with
  mdisp/pdisp/vdisp and attributes).
- `__RTtypeid`: the complete object's descriptor; a null pointer throws `std::bad_typeid`.
- `__RTCastToVoid`: the complete object (its offset, and the construction displacement).
- `__RTDynamicCast`: C++11 5.2.7/8 over the flattened base array: the dst subobject holding the src one (a
  down-cast), else, with src public in the whole, dst an unambiguous public base (a cross-cast). A failed
  reference cast throws `std::bad_cast`. RTS6x's Itanium `__dynamic_cast` is the model of the algorithm.
- `type_info::name()`: the decorated name undecorated as vcruntime spells it ("struct Both",
  "char const * __ptr64", "class std::vector<int>"), cached in the descriptor's spare word.
- cpp11 1.30 refuses a polymorphic non-first base on Microsoft's ABI. Virtual bases and several polymorphic
  bases are handled by the algorithm but not yet produced by the compiler.

### 8.4 Exceptions (in widening, before cppparse is an exit test)
**Why widening and not later:** cppparse (73 lines of try/throw/catch) and the toolchain demo's `shapes.cpp`
throw. Both are exit tests from stage 2 on, and the toolchain demo runs in stage 3's migration check.

What it needs, all in the layouts cpp11 already emits:
- `_CxxThrowException(object, ThrowInfo)` calls `RaiseException` with code 0xE06D7363, the magic
  0x19930520, the object, the ThrowInfo and the image base.
- `__CxxFrameHandler3` works in two passes:
  - **search:** the FuncInfo (magic 0x19930522: the unwind map, the try map with its handlers, the
    ip-to-state map) gives the state of the faulting ip. Each try block covering it is matched against the
    thrown CatchableTypes (type descriptor, cv and reference rules, the base displacement).
  - **unwind:** on a match, `RtlUnwindEx` to the catch's frame, whose unwind pass calls back to run the
    unwind map's destructors down to the try's state. The object is copied into the catch parameter (by its
    copy constructor, or bit by bit), the catch funclet is called, and execution continues at the address
    it returns.
- Rethrow (`_CxxThrowException(0, 0)`), `std::uncaught_exception`, and the object's destruction when its
  last catch ends: a per-thread record of the exceptions in flight.
- `std::terminate`/`set_terminate`, and `std::unexpected` for dynamic exception specifications.
- An uncaught exception: Microsoft's unhandled-exception filter sends a C++ exception to `terminate`,
  which `abort`s. In Release that prints nothing and exits 0xC0000409; GRIDE's does the same.
- `RaiseException`, `RtlUnwindEx`, `RtlLookupFunctionEntry`, `RtlVirtualUnwind`, `RtlPcToFileHeader` and
  `RtlCaptureContext` are all kernel32 exports on x64, so they go in `kernel32.def`, with no ntdll import.
- Before this step a throw ends the program as an uncaught one does. The Debug variant also writes what was
  thrown.
- The exit tests: cpp11's 706 cases on GRIDE's runtime give the same verdicts as on Microsoft's; cppparse;
  the toolchain demo.

---

## 9 Machine-level contracts

### 9.1 Calling convention
Every function uses Microsoft's x64 convention:
- arguments in rcx, rdx, r8, r9 (xmm0-3 for floating point), with 32 bytes of home space reserved by the
  caller and rsp 16-aligned at the call; the result in rax or xmm0;
- rbx, rbp, rdi, rsi, r12-r15 and xmm6-15 kept by the callee;
- the direction flag clear on entry and exit; MXCSR and the x87 control word kept.

A variadic function's floating-point arguments are passed in both registers, so `va_arg` walks one array of
8-byte slots.

### 9.2 Unwind data for the MASM routines
- A routine that moves rsp or saves a non-volatile register is a `PROC FRAME` with `.PUSHREG`,
  `.ALLOCSTACK`, `.SAVEREG`, `.SAVEXMM128` and `.ENDPROLOG`. That lets the debugger walk through it and an
  exception unwind through it. GRIDE's masm writes `.pdata`/`.xdata` for these.
- A leaf that uses only volatile registers and leaves rsp alone needs none (`__chkstk`, `fabs`, `_setjmp`).
- **GRIDE's masm needs about 15 instructions added first** (stage 1, before the SSE2 work):
  - its table has scalar SSE2 and `MOVDQA`/`MOVDQU`/`PXOR`/`PAND`/`POR`;
  - it lacks `PCMPEQB`, `PMOVMSKB`, `PMINUB`, `PSHUFD`, `PUNPCKLBW`, `MOVNTDQ`, `MOVNTI`, `ROUNDSD`,
    `STMXCSR`, `LDMXCSR`, `FNSTCW`, `FLDCW` and `FNCLEX`.

  Bytes written with `DB` cannot carry a relocated memory operand, so they are a stopgap only for
  register-addressed operands.

### 9.3 CodeView for the runtime, and stepping into it
- Every library variant is compiled with `-g`, so a program's PDB carries the runtime's functions and lines
  and gride-dbg can show them and step into them. This needs cpp11's `-g` fix (section 2).
- **Stepping is the user's choice.** gride-dbg's step filter is by `module!name` today. A statically linked
  runtime is in the program's own module, and once its sources are installed it "has source". So the filter
  becomes one by **source path**: code whose source lies under the runtime's source folder is stepped over
  by default, as Microsoft's CRT is, whose sources are not on the machine. An Options setting, "Step into
  GRIDE's runtime", turns that off.
- **Callbacks still stop.** Stepping over `qsort` stops in the program's comparator if it has a breakpoint,
  and stepping into `qsort` with the runtime filtered lands in the comparator. The same holds for `atexit`
  functions, the new-handler, signal handlers and C++ initialisers.
- The runtime's sources are installed with GRIDE (`src\runtime\`), and a source-path map in gride-dbg turns
  the PDB's paths (the build machine's) into the installed ones.

### 9.4 Header declarations
One C header set (section 5.2), checked at build time against the catalogue (section 14.2). Every catalogue
function is declared by the headers its entry names, at the level it names, and nothing public is declared
that is not in the catalogue. A test program that references every function the shipped headers declare
must link against GRIDE's runtime alone (section 11.8).

### 9.5 Data the compilers' code reaches
`stdout` is `__acrt_iob_func(1)` and `errno` is `*_errno()`, so FILE's layout is private and can change
without recompiling a program. The one exception is thread-local storage, which cpp11's code reaches
directly through `_tls_index` and gs:[88]. Its layout is the PE TLS directory's, fixed by Windows.

### 9.6 Threads
- cpp11's `windows.h` offers `CreateThread`, and cppparse starts threads (`common\Parallel.cpp`). An
  allocator or a stream without locks would corrupt memory once a second thread runs.
- **The creating thread sets the flag.** `grtwin` defines `CreateThread` (and later `_beginthreadex`) over
  `__imp_CreateThread`. `kernel32.def` declares `CreateThread` as `DATA`, so the import library defines
  only `__imp_CreateThread` and no thunk competes with the wrapper. The wrapper sets "threads exist" before
  the new thread can run. From then on the allocator, the streams, `atexit` and the static guards take an
  SRW lock; a program that never starts a thread never takes one.
- The TLS callback (DLL_THREAD_ATTACH) stays as a backstop for threads Windows starts itself (the thread
  pool, Ctrl+C's handler). Those start before they can touch the runtime's state.
- errno, strtok's state and the formatter's statics are per-thread, in static TLS, the machinery cpp11's
  `thread_local` uses.
- GRIDE's link already writes the TLS directory from `_tls_used` (`LINK\src\image.cpp`). The runtime supplies
  `_tls_used`, `_tls_index`, the `.tls$` start and end markers, and the `.CRT$XLA`..`XLZ` callback array.
- **The locks, as the UCRT takes them** (built 2026-10-27; section 16). Each call decides once, at its start,
  whether it locks, and unlocks only what it locked:
  - *Descriptors* (`win\files.c`): each read, write, seek, close and `_setmode` holds its descriptor's lock. A
    free descriptor is claimed under the table's lock and stays claimed, neither open nor free, until its file
    is open, so two opens never get one descriptor. A close frees the descriptor by its last store.
  - *Streams* (`c\StreamWin.h`): every public function that takes a `FILE` locks it for the whole call, so one
    `printf` is one piece of the output. The stream layer's own functions and `Stream`'s members take no lock,
    so a call can use any number of them, as the UCRT's `_nolock` functions are used. A free slot of the
    512 is claimed under the stream table's lock. `fflush(NULL)` and the end of the program hold that lock,
    then each stream's. Nothing takes the table's lock while holding a stream's, so the two cannot deadlock.
    As the UCRT does, `fflush` passes over a stream with nothing waiting to be written without taking its
    lock, and so does the end of the program for the standard streams. A thread blocked reading stdin, which
    holds stdin's lock, then holds up neither.
    `freopen` keeps its `FILE` claimed between the close and the open.
  - *The character functions* keep their fast path, the buffer's next byte, while there is one thread. With
    threads they lock and take the same path under the lock. `feof`, `ferror` and `_fileno` read one word and
    lock nothing, as the UCRT's do.
  - *atexit* (`c\atexit.cpp`): cpp11 registers a function-local static's destructor from whichever thread makes
    the static first, so the list is changed under a lock.
  - Locks are SRW locks: one zero word each, in the `FILE`, the descriptor and the tables, so none needs
    initialising. They are not recursive, so no locked call calls another public function on the same stream.

---

## 10 Efficiency

### 10.1 Rules
- The runtime is compiled at -O2 by our compilers. Where they produce poor code for a hot routine, the
  routine is written in MASM.
- Only what a program reaches is linked: one function (or one small family) per object, so `/OPT:REF`
  leaves the rest out. Floating-point formatting sits in its own objects, so a program that prints no
  floats does not carry the float formatter.
- No locale or thread-safety cost on the hot paths beyond what C requires (section 9.6).

### 10.2 memory and string
- **`memcpy`/`memmove`/`memset`** (MASM), tiered by size; the thresholds are tuned on the benchmark, with
  vcruntime's `memcpy.asm` as the reference for the shape:
  - below 16 bytes, overlapping scalar moves from both ends;
  - 16 to 128 bytes, unaligned SSE2 from both ends;
  - up to a threshold, an aligned SSE2 loop;
  - above it, `rep movsb`/`rep stosb` where CPUID reports ERMSB;
  - very large blocks with non-temporal stores.
- **`strlen`, `strchr`, `memchr`** (MASM, SSE2): aligned 16-byte loads, which never cross a page, with the
  leading bytes masked off. **`strcmp`, `strncmp`, `memcmp`** cannot align both strings: they compare by
  aligned loads of one and check the other for a page crossing (its offset within the page past 4096-16),
  dropping to bytes there.
- **The allocator:**
  - Size classes: 16-byte steps to 128 bytes, then quarter-powers of two to 4 KB. Each class has a free
    list.
  - The space for small blocks is one region reserved with `VirtualAlloc` and committed in 64 KB slabs as
    needed. A block's class is found without a header: a side table indexed by `(address - region) >> 16`
    names each slab's class. A pointer outside the region is a large block, freed to the process heap.
  - When the region is full, another is reserved (up to 16), and only then do small blocks go to the
    process heap.
  - Blocks are 16-byte aligned, as Microsoft's are. `realloc` grows in place within a class, and moves
    otherwise.
  - The Debug variant puts a header and guards on every block (Microsoft's fill bytes, section 4.3), so its
    layout differs from Release.
  - Locked only once a thread exists (section 9.6).

### 10.3 Standard I/O
**A stream layer of Windows' own.** RTS6x's stdio writes through to its host on every call (`fflush` is a
no-op), reads through a 512-byte buffer with a position query per text fill, and has no `setvbuf`,
`fgetpos`, `fsetpos` or `tmpnam`. That design is the C6747's, whose host channel is the bottleneck.
- Windows gets its own `Stream` implementation in grt's sources (separate files, not `#if` forks of
  RTS6x's), with RTS6x's Formatter, Scanner and FloatText unchanged above it.
- The C6747 keeps RTS6x's stdio exactly as it is. Section 11.7's "C6747 code unchanged" stays exact.
- The buffer belongs to the FILE, where the call boundary is known, not to the descriptor. So `_write`
  stays unbuffered, as C programmers expect of it.

**The buffering policy** follows Microsoft's, so a crashing or aborting program leaves the same bytes:
- **Files, pipes and NUL: fully buffered.** The buffer is 4,096 bytes, the UCRT's size, so what is on disk
  when a program dies is what it would be there; `setvbuf` changes it. A large `fwrite`/`fread` goes straight
  through once the buffer is empty, as the UCRT's does. The buffer is flushed when full, by `fflush`, before
  a seek, before a read of the same stream, and at `exit`. Text mode translates into the buffer.
- **A console: buffered for the length of one call, flushed at its end** (Microsoft's "temporary
  buffering"). One printf is one `WriteFile`, a prompt shows before the program reads, and gride-dbg sees an
  unfinished line at a stop. *This deviates from "line-buffered only to a console".* The reasons:
  - gride-dbg runs programs on a pseudo-console and shows the unfinished line at a stop; line buffering
    would hide it;
  - line buffering would also reorder stdout against stderr compared with Microsoft's.

  A `putchar` loop to a console is therefore one `WriteFile` per call, as with Microsoft's. Microsoft keys
  this on `_isatty`, which includes NUL. GRIDE's keys it on `GetConsoleMode`, so printf to NUL is fully
  buffered: the same bytes, written faster.
- **stderr: unbuffered**, flushed at the end of each call, as C requires and Microsoft does.
- **Reading:** standard input from a console is read as a person types it; from a pipe or a file it is read
  in blocks of 4 KB, with no position query per fill.
- **printf** copies runs of plain text in the format at once. `%d`, `%s` and `%c` skip the full spec parse.
  Integers use two-digit tables (RTS6x's). `%zu`, `%td` and Microsoft's `%I64d`, `%I32d`, `%Id` read the
  right width. Floating point stays exact; a faster digit generator for `%.Nf`/`%e`/`%g` with N <= 17
  (Ryu-printf style) comes once measured.
- **scanf** has a fast path for `%d`, `%s` and `%lf` that reads from the stream's buffer directly, without a
  per-character call; the general Scanner remains for the rest.
- Shortest float formatting (Shalimar's `Shortest.cpp`) moves into RTS6x's sources, so it serves Shalimar
  on both targets and C++'s number output (section 14.4).

### 10.4 math
- `sqrt` is `sqrtsd`, followed by a test that sets errno to EDOM for a negative argument, as the UCRT does.
- `floor`, `ceil` and `trunc` use `roundsd` with SSE4.1, and bit operations without it.
- `round` (half away from zero) is not a `roundsd` mode: it is `trunc`, plus one away from zero when the
  dropped part is a half or more.
- `exp`, `log`, `sin`, `cos` and `pow` keep RTS6x's accuracy (within 1 ulp of the UCRT today). Each gets a
  faster kernel where the benchmark shows it slower than Microsoft's, with that accuracy kept.

### 10.5 The benchmark harness
`toolchain\Windows\RT\bench\` holds the benchmarks. Each is a C program (c90 -O2) that times itself with
`QueryPerformanceCounter`, best of five runs. Each is built twice by the same compiler and flags, once
against GRIDE's runtime and once against Microsoft's static CRT, and run on the same machine. The set:
- printf of ints, floats and strings, to a file and to NUL, at least 1M calls each; scanf of ints and
  floats;
- fwrite/fread and fgets of a 64 MB file;
- memcpy/memset/memmove at 16 B, 256 B, 4 KB, 64 KB and 1 MB; strlen/strcmp/strchr;
- malloc/free churn of small and of mixed sizes;
- qsort of 1M ints;
- sqrt, sin, exp, log and pow throughput;
- a hello-world's start-to-exit time and its .exe size.

The results go into `bench\results.md` (machine, date, both times, the ratio) and into each runtime commit's
message. **The rule:** from stage 2's exit on, every benchmark must be within 5% of Microsoft's time or
faster, the 5% being the measurement's noise. A slower one is a bug against the runtime. Stage 1 records the
benchmarks, and it is slower than the prototype on none of them.

---

## 11 Correctness

1. **The example sets** (both of errtest.py's, Debug and Release) go through GRIDE's engine twice. Once on a
   GRIDE copy that finds no Visual Studio, with LIB, INCLUDE and PATH cleared of Visual Studio and the SDK;
   once with Microsoft's CRT. The checks:
   - every program's stdout and stderr byte for byte, its exit code and the expected lines;
   - the imports: `KERNEL32.dll` alone;
   - every injected error reported at the same place.
2. **UCRT test vectors**, kept in `toolchain\Windows\RT\tests\`. Each is built against both runtimes and
   compared; the reference outputs are recorded, so the test runs where there is no Visual Studio. The set:
   - the C library probe;
   - 1,838 printf/scanf/strtod/atof cases, including 400 random doubles to 40 digits;
   - NaN spellings;
   - the math functions in ulps;
   - the C++ ABI and library probes;
   - a signal probe (SIGSEGV, SIGFPE, SIGINT);
   - exit-code probes (`abort`, a failed `assert`, a pure virtual call, an uncaught exception).
3. **Microsoft's choices that C leaves open are followed:**
   - `rand`'s sequence;
   - `%p` as 16 upper-case digits;
   - `%a` with 13 digits, subnormals as `0x0.`;
   - the NaN spellings (`nan(ind)`, `nan(snan)`, every NaN's sign);
   - scanf failing an incomplete field;
   - strftime's `%c` and asctime's two-digit day in the "C" locale;
   - `strerror`'s words;
   - the exit codes of section 4.3.
4. **The debugging exercise** (exercise.py's twelve examples) on GRIDE's runtime: the same stops.
5. **cppparse** built by GRIDE with each runtime, `--health` run on its sources, and the outputs compared.
   This is an exit test from stage 2, with exceptions.
6. **cpp11's 706 cases** with exceptions.
7. **The C6747 unchanged.** RTS6x's C6747 assembly is compared before and after, with line-number directives
   left out, and the C6747 examples run on sim6747 and vm6747. The one intended change is RTS6x's two bug fixes
   (section 15). They are a commit of their own for both targets, which changes `log`'s and `pow`'s C6747 code
   on purpose and is checked by the C6747 examples.
8. **Completeness.** A test program referencing every function the shipped headers declare links against
   GRIDE's runtime alone. So does a Shalimar program using every built-in shalimar can emit. Each stage's
   catalogue says what is expected to be there.

---

## 12 Build

### 12.1 Tools
c90, cpp11, GRIDE's masm and GRIDE's link, all from the same solution build (`x64\<Config>`). GRIDE's link
gained `/LIB` for this:
- archives, with both linker members, longnames and deterministic headers;
- import libraries from a `.def`, in the SDK's layout;
- `/LIST`;
- `@response` files.

GRIDE's masm gains the instructions of section 9.2.

### 12.2 Sources and lists kept in the repo
- `toolchain\Windows\RT\` holds:
  - the project, `grt.vcxproj`;
  - `rts6x.sources`, which RTS6x files each library takes;
  - grt's own `src\`, by library (`start\`, `compat\`, `cxx\`, `c\`, `win\`);
  - `kernel32.def`;
  - `catalogue.json` (section 14.2);
  - `tests\` and `bench\`.
- RTS6x's sources stay in `toolchain\TI\RTS6x\src`. Windows-only changes in them are `#if defined(_WIN32)`
  branches, kept to few; the C6747's code is compared before and after every change (section 11.7). The
  stream layer is grt's own (section 10.3).

### 12.3 MSBuild targets (`toolchain\msbuild\Toolchain.targets`)
- `WindowsLibrary`: sources through c90/cpp11 (-S -masm=masm) and masm, packed by `link /LIB`.
- `LibraryClosed`: the libraries together leave nothing undefined but `main` (and `wmain`/`WinMain` later).
- `LibraryLayers`: no name defined twice across the libraries; each library's undefined names defined in
  itself or below it (section 4.2); only `grtwin` imports kernel32.
- `FrameCheck`: no c90-compiled function's frame reaches 4 KB, until c90 probes the stack.
- `CatalogueCheck`: the headers and the libraries against the catalogue (section 14.2).
- The import library: `link /LIB /DEF:kernel32.def`.

No step runs through cmd.exe. Visual Studio's Build Solution runs them, in the grt project, which builds
after c90, cpp11, masm and link.

---

## 13 Packaging

### 13.1 GRIDE's choice of runtime
- toolchain.json `libraries` has an entry per runtime, `kind: "hostCrt"`:
  - `crt: "grt"`: GRIDE's runtime, the five libraries and `kernel32.lib` by path, from
    `lib\x86_64-windows`;
  - `crt: "msvc"`: Microsoft's, by name, found through LIB.

  The Shalimar runtime likewise has one entry per `crt`.
- Tools > Options > Projects > GRIDE Paths > **Windows C runtime**: GRIDE's runtime / Microsoft's CRT /
  automatic. settings.json `windowsRuntime` (`grt`, `msvc`, `auto`); `GRIDE_WINDOWS_RUNTIME` at start wins.
- **Automatic** takes Microsoft's only where Visual Studio is found **and** the build has cl's objects or
  links a library that names Microsoft's runtime by `/DEFAULTLIB`. Otherwise it takes GRIDE's, always where
  Visual Studio is absent.
- With GRIDE's runtime, the link gets the libraries by their full paths: no LIB, no vcvars. The Output pane
  says which runtime a link took, once per project.

### 13.2 Both runtimes from the same objects
The headers name only what both runtimes provide (section 5.2), so a program compiled once links with
either. That lets the correctness test (section 11.1) compare the two, and lets a user switch without
recompiling.

### 13.3 Setup
- build-setup.ps1 packs `x64\Release` as it does. For GRIDE it requires the runtime's libraries and
  `kernel32.lib` in `lib\x86_64-windows`, and fails without them.
- test-setup.ps1 checks, for GRIDE:
  - the libraries are present;
  - `link /LIB /LIST` shows `mainCRTStartup` and `__imp_ExitProcess`;
  - a C program built by the installed toolchain and linked with GRIDE's runtime alone (LIB and INCLUDE
    empty) runs, and imports `KERNEL32.dll` alone;
  - no program of the toolchain imports a VC++ runtime DLL.
- The toolchain's programs are built with the static VC++ runtime (`/MT`, `/MTd`). Only GRIDE.exe (C++/CLI)
  keeps the DLL runtime, which Setup puts beside it.

### 13.4 Libraries built by cl
A prebuilt library compiled by cl (the third-party example's `mathx.lib`) names `LIBCMT` and `OLDNAMES` with
`/DEFAULTLIB`, and GRIDE's link honours `/DEFAULTLIB`. With GRIDE's runtime:
- the link is given `/NODEFAULTLIB:` for each of Microsoft's runtime libraries (`libcmt[d]`, `msvcrt[d]`,
  `libucrt[d]`, `ucrt[d]`, `libvcruntime[d]`, `vcruntime[d]`, `libcpmt[d]`, `msvcprt[d]`, `oldnames`,
  `legacy_stdio_definitions`), and the Output pane says which were asked for;
- `grtcompat` supplies what cl's code calls beyond the C library: `_fltused`, `/GS`'s cookie and handlers,
  and, with exceptions, `__CxxFrameHandler4` and `__std_terminate`;
- a name still missing is reported by the link as usual. The Output pane adds that the library was built
  for Microsoft's runtime.

`mathx.lib` needs only `_fltused`, so it links once its `/DEFAULTLIB` is refused.

---

## 14 Shalimar's direct access (stage 4; the catalogue from the baseline)

### 14.1 How Shalimar reaches its runtime today
- shalimar emits calls to `shm_*` functions (`shm_print_int`, `shm_line`, `shm_array_make`, ...): plain C
  names, `extern "C"` in `runtime\shmrt.h`.
- For its built-ins it calls C library names (`Builtin.cpp`'s table: `sqrt`, `sin`, `pow`, `fmod`, `trunc`,
  `hypot`, `log2`, `cbrt`, `fabs`, ...).
- The program defines `shm_user_main`, `shm_init_globals` and `shm_name_files`; the runtime's `main` calls
  them.
- A name not in the table is refused. Where its C signature needs a type Shalimar has no form for, the
  refusal says why (`Builtin.cpp`'s unborrowable list: pointers, varargs, out-parameters).
- Shalimar has no headers, and toolchain.json gives it no include or define flag.

### 14.2 A stable public C ABI, and one catalogue
- Every public runtime function:
  - has a plain C name and keeps it across versions;
  - follows Microsoft x64's convention, and the C6000 EABI's where RTS6x is the implementation;
  - has documented argument and result types.

  C++ support stays internal behind it.
- **`toolchain\Windows\RT\catalogue.json`** is part of the baseline: one entry per public function or
  object. Each entry has:
  - its name, component, header(s) and level (C90, C99, C11, C++03, C++11, Microsoft, POSIX, GRIDE);
  - **structured parameter and result types** rather than a signature string (`int32`, `int64`, `double`,
    `size` - 64 bits on x86_64-windows, 32 on tms6747 - `long` - 32 bits on both - `pointer`, `text`);
  - **per-target link names** (`errno` is `_errno()` on Windows and `__c6xabi_errno_addr()` on the C6747;
    `stdout` is `__acrt_iob_func(1)` and `&_ftable[1]`);
  - its library, stage and status;
  - how it reports failure;
  - the Shalimar mapping (section 14.3).
- From the catalogue come:
  - the header check (section 9.4);
  - the library check (every entry defined in the library it names);
  - the `.def` of a future DLL;
  - Shalimar's built-ins.

### 14.3 How a Shalimar program will name a runtime function
- **Proposed: a table generated from the catalogue and compiled into shalimar.** It replaces `Builtin.cpp`'s
  hand-written rows: each entry the catalogue marks usable from Shalimar becomes a built-in with its shape.
  - No new syntax, and no file to find at compile time.
  - The compiler can never disagree with the runtime it ships with.
- **Names:** a program's own definition shadows a runtime built-in of the same name, so a program with its
  own `time` or `remove` keeps compiling. A qualified form (`c.remove`) always reaches the runtime's. Both
  rules are fixed now, before the first catalogue built-in, so no program changes meaning later.
- **Considered and not proposed:**
  - an import list read at compile time from `lib\<target>\shalimar\`: extensible without rebuilding the
    compiler, but a missing or stale file breaks programs;
  - a module syntax (`use c.math`): it belongs to Shalimar's language design, and can come later over the
    same table.

Type mapping:

| Shalimar | C | Notes |
|---|---|---|
| int | `int32_t` | by value |
| real | `double` | by value |
| char | `int32_t` holding an unsigned char | |
| text, arrays | `ShmArray *`: `{count, element kind, data}`; `real[]` and `real[][]` as nested descriptors | a C function taking `const char *` gets a wrapper converting the text (the runtime's, not the program's) |
| a returned `char *` (`getenv`, `strerror`) | copied into a text | a null pointer is the empty text, or a failure where the catalogue says so |
| a `size_t` or `long` result | int, checked | a value past int's range is a failure |
| by-reference parameter | a pointer to the variable | out-parameters (`modf`, `frexp`) become two-result built-ins |
| errors | `errno` and a function's error result | the catalogue entry says how the function reports failure. Shalimar's built-in checks it and calls the runtime's **new** `shm_fail(message)` (today only an internal `fail()` exists) with "sqrt: Domain error", or returns the C result where the entry says that is Shalimar's meaning (NaN) |

### 14.4 shmrt on top of the runtime, not beside it
- **Moves down:**
  - shortest float formatting (`Shortest.cpp`) goes into RTS6x's own sources, as a GRIDE extension
    (`_gride_shortest_double`, in the catalogue). It is used by shmrt and shmrt6x, and available to C/C++.
  - the fixed-places formatting shmrt does with `snprintf` stays a call to the C library.
- **Stays in shmrt:** Shalimar's semantics:
  - the failure messages and their line;
  - the array descriptors and their operations;
  - the console's column layout;
  - the recursion limits;
  - the debugging session (SHM_DEBUG).
- No duplicate implementations: shmrt calls the runtime for formatting, memory and I/O.

### 14.5 Both targets
The catalogue's per-target names and types say which functions exist on tms6747 through RTS6x, so the same
Shalimar built-ins compile for the C6747. shmrt6x is built over RTS6x as it is now.

---

## 15 What the prototype found

The prototype (branch `grt-prototype`) built one `grt.lib` from 266 RTS6x files and 22 of its own, a
`kernel32.lib` from a 52-function `.def`, and the Shalimar runtime over them; GRIDE chose it automatically.
It measured:
- **Identical to the UCRT, byte for byte**, at -O2 and -O0:
  - a C library probe: stdio to files and back, scanf from stdin, argv quoting, getenv, errno/perror,
    qsort, strtod/strtol, ctype, strftime, asctime, setjmp, signal, atexit, the exit code;
  - 1,838 float printf/scanf/strtod/atof cases, and NaN parsing;
  - the C++ ABI probe: static construction and destruction order, a local static, dynamic_cast, typeid
    names;
  - a C++ library probe: iostream, sstream, string, vector, map, set, the new/delete forms.
- **math**: 1,726 of 1,739 results identical to the UCRT's; the other 13 are within 1 ulp.
- **Both example sets** (180 builds: 60 as-is programs run, 120 with injected errors) on a GRIDE copy that
  finds no Visual Studio, with LIB and INCLUDE cleared, against the same on Microsoft's CRT:
  - 178 cases identical, stdout and stderr byte for byte; each program imports `KERNEL32.dll` alone;
  - the two others are `mathx-cpp`, which did not link: a prebuilt cl library naming `LIBCMT`
    (section 13.4).
- **The debugging exercise** on GRIDE's runtime with no Visual Studio gave the same 80 moves, 70 stops and
  10 runs to the end as the first run, every stop on the same line.
- **cppparse**, built by GRIDE with each runtime, gave the same 3,014-line `--health` report.
- **Size**: hello world is 70 KB, against Microsoft's static 143 KB.
- **Speed**: cppparse's `--health` took 3.2 s on GRIDE's runtime against 2.8 s on Microsoft's. The
  prototype writes through on every call and allocates with HeapAlloc each time; this design replaces both.

**Compiler bugs** are listed in section 2, with the owners' fixes as stage 1's entry criteria.

**RTS6x bugs** were fixed for Windows in `#if defined(_WIN32)` branches. Stage 1 fixes them for both targets
in a commit of their own, which the C6747 examples check:
- `math\LogarithmNatural.cpp`: `return kernel(x, lo) + lo;` depends on the unspecified order of evaluation;
  x86-64 code read `lo` first, and `log` was wrong.
- `math\PowerRaise.cpp`: `pow(-1, y)` with |y| past 2^64 answered 0 (C99 F.9.4.4: 1, or -1 for an odd y).

---

## 16 The stages

| Stage | Contents | Exit test |
|---|---|---|
| 0 Design | this document, reviewed and accepted | the go-ahead |
| 1 Baseline | **first steps, each a commit under its tool's gates:** the compiler fixes of section 2 (cpp11, then 1.30; c90's stack probe and its -O0 setjmp bug); GRIDE's masm's instructions (9.2), checked against ml64; RTS6x's two bugs fixed for both targets. **Contents:** the five libraries and their layers; start-up and exit (7); the Windows layer (6); `__chkstk` and mem* (MASM, SSE2/ERMSB); the Windows stream layer with the buffering of 10.3; enough of stdlib, string, ctype and math for the example sets and every name shalimar emits; `operator new`/`delete` and `_purecall`; Release behaviour for `abort` and invalid parameters; `/NODEFAULTLIB` of Microsoft's libraries (13.4); `kernel32.lib` generated; `catalogue.json` with every baseline function; the build checks of 12.3; the benchmark harness | both example sets (C, C++ without throws, Shalimar) identical to the Microsoft-CRT builds with Visual Studio cleared; the UCRT probes; the completeness test for the baseline's catalogue; the benchmarks recorded, none slower than the prototype's. Committed and pushed |
| 2 Widening | the rest of the C library (C99 additions, `setvbuf`/`fgetpos`/`tmpnam`, files past 2 GB, the io.h/direct.h/stat layer, `system`, signals' SEH and Ctrl+C); C++ support (RTTI, statics, thread_local); **exceptions** (8.4); threads (9.6); the allocator; SSE2 str*; printf/scanf fast paths; math kernels; the Debug variants, CodeView and gride-dbg's source-path filter; GRIDE's Options choice; Setup and its test; the toolchain's programs on /MT | + the debugging exercise, cppparse, cpp11's 706 cases, the toolchain demo, the Setup test, every benchmark within 5% of Microsoft's or faster |
| 3 Layout | the header and library tree of section 5 and its migration | + the C6747 examples on sim6747 and vm6747, before and after |
| 4 Shalimar's direct access | section 14: the generated built-in table, the name rules, the type mapping, `shm_fail`, shmrt over the runtime, Shortest moved down, both targets | + Shalimar access tests on x86_64-windows and tms6747 |
| later | `wmain`/`WinMain`; UTF-8 console and paths (resources in GRIDE's link for a manifest, or a runtime switch); wide characters; a DLL variant if wanted | |

**Stage 1 as built (2026-10-23).**

The first steps:
- cpp11 1.30, c90's fixes and GRIDE's masm's instructions (section 2);
- RTS6x's log and pow fixed for both targets;
- RTS6x's C99 log2, hypot, cbrt, strtoll, strtoull, strtof, strtold and vsnprintf, for both targets;
- link /LIB;
- the toolchain's programs on /MT.

Then the runtime itself, in `toolchain\Windows\RT`:
- the five layered libraries, with LibraryClosed (Layered), FrameCheck and CatalogueCheck in the build;
- the Windows stream layer of 10.3, with a null `FILE *` failing fast;
- SSE2/ERMSB mem*, and sqrtsd;
- `catalogue.json` (157 entries, 147 done; the rest are io.h/direct.h/stat and `system`, stage 2);
- the benchmark harness (`bench\`).

The exit test:
- both example sets: 180 of 180 builds, Debug and Release, identical to Microsoft's CRT with Visual Studio cleared. Every program imports `KERNEL32.dll` alone, and `mathx-cpp` links;
- the UCRT probes, byte-identical at -O2 and -O0 (math identical or within 1 ulp);
- the completeness test: 141 of 141 link names;
- the debugging exercise, unchanged;
- the benchmarks recorded in `bench\results.md`, slower than the prototype on none.

Left for stage 2 by the numbers there (Microsoft's static CRT = 1):
- str* about 4-44 (RTS6x's byte loops: the SSE2 str* of 10.2);
- sin, exp, log and pow about 2-3.7 (10.4's kernels);
- printf to a file about 1.3-1.7 (10.3's fast paths);
- memcpy at 4 KB, where bench.c's two static arrays alias by 4 KB.

**Stage 2 so far (2026-10-23).** The mem* tiers are done (550c6b1): AVX where Windows has it, the 4 KB aliasing case taken backward, no loop to 256 bytes. memcpy is 0.35-1.01x Microsoft's, memset 0.93-1.00x, memmove 0.71x. The work stopped there for the account's usage budget; the state and the next steps, in order, are in the shared notes (gride-runtime-progress.md). Found on the way, to fix in stage 2: the UCRT returns an sNaN quieted, sets no ERANGE on exp's and pow's underflow, and has its own pow NaN rules; its printf fails fast on %n unless _set_printf_count_output is on, and pads %s with zeros under the 0 flag.

**The filing functions and `system` (2026-10-26).** `_getcwd`, `_findfirst64i32`, `_findnext64i32`, `_findclose`, `_stat64i32` and `system` are in `grtwin` (`win\getcwd.c`, `findfile.c`, `stat.c`, `system.c`); the catalogue has 153 of 157 done, `_access`, `_chdir`, `_mkdir` and `_rmdir` left. c90's `<stdlib.h>` declares `system`.
- **`system` has no command processor of GRIDE's own.** As the UCRT's, it runs `%COMSPEC% /c <command>`, else `cmd.exe` from the working folder or PATH. A command means what the processor makes of it, so a program does what it does on Microsoft's runtime; where cmd.exe is refused by policy, it prints the refusal and `system` answers cmd's exit code (255).
- `tests\filing\filing.ps1` builds one probe, links it with each runtime and runs it on a tree it makes; the transcripts are identical (146 lines, recorded in `filing.expected`). The UCRT's `stat` gives `st_nlink` 1 for a hard-linked file: its sources' GetFileInformationByName path is not in the shipped library, and grt follows the library.
- cppparse builds and links on GRIDE's runtime in Release and Debug, importing KERNEL32.dll alone, and `--health . -j 1` prints what Microsoft's build prints. With its worker threads it crashes: the descriptor and FILE tables take no lock yet (9.6).

**The tables locked (2026-10-27).** The locks of 9.6 for the descriptors, the streams and the `atexit` list, behind `grtwin`'s `CreateThread` (`win\thread.c`; `kernel32.def` exports `CreateThread` as `DATA`).
- RTS6x's stdio functions that take a `FILE` are now `grtc`'s own files (`c\fprintf.cpp` ...): RTS6x's bodies with the stream's lock around them. RTS6x's sources are unchanged, so the C6747's code is too. `fopen`, `feof`, `ferror` and `perror` stay RTS6x's, and so does `atexit`'s list, which `c\atexit.cpp` changes under a lock.
- cppparse is built by GRIDEd's engine on GRIDE's runtime, importing KERNEL32.dll alone, and runs `--health .` with its worker threads. In 30 runs of 30 in Release and 30 of 30 in Debug, stdout (423,926 bytes), stderr and the exit code are byte-identical to the build on Microsoft's CRT. Each run takes 3.0 s against Microsoft's 3.0 s in Release, and 5.1 s against 10.1 s in Debug. The same build on the runtime before the locks failed 5 runs of 5 here: four access violations, and one run that printed 817,703 bytes.
- The UCRT probes are unchanged: libc, floatvec, nanparse, abi, cxx, mathlogpow, stdio1, c99lib, memtest and nullfile byte-identical at -O2 and -O0, mathvec's 13 rows within 1 ulp, c99math's differing rows the correctly rounded ones. `tests\filing\filing.ps1` gives 146 of 146 lines on each runtime, and the completeness test links 147 of 147 names. `tests\threads\threads.ps1` runs a probe of eight threads. Each opens, writes with `fprintf` and `putc`, reads back and closes its own files 300 times, writes whole lines to one shared `FILE`, and makes function-local statics. Its transcript matches the UCRT's (`threads.expected`) in 30 runs of 30. On the runtime before the locks, 10 runs of 10 ended in an access violation or a corrupted heap.
- A program with one thread takes no lock. On the benchmarks (`bench\results.md`, which gains putc and getc, 64 MB a byte at a time), no ratio to Microsoft's moved by more than the noise. getc pays 2 % for the fast path's test of the flag, and putc and getc stay about 0.2 of Microsoft's time, whose functions lock on every call.
- Still single-threaded, for the rest of 9.6: errno, strtok's state and `localtime`'s result are shared, not per-thread (static TLS is not built yet). A thread started without the wrapper is not seen, for example by a cl-built object that calls `__imp_CreateThread` directly; the TLS callback that would see it is not built either.

---

## 17 Decisions (decided 2026-10-23)

| # | Question | Decision |
|---|---|---|
| 1 | Static libraries only (4.1), or a DLL variant planned? | Static only; the DLL is kept possible (4.4) |
| 2 | Five libraries in layers (4.2), or one `grt.lib`? | Five layered libraries and `kernel32.lib`, with the build checks of 12.3 |
| 3 | Console output flushed at the end of each call, as Microsoft's (10.3), instead of line buffering? | Accepted: identical output and the debugger's view of an unfinished line outweigh line buffering |
| 4 | The 64-bit `time_t` for c90 too (5.2)? | Accepted, with the 32-bit names in `grtcompat` |
| 5 | Exceptions in stage 2 (8.4)? | Accepted |
| 6 | The layout and its migration (5.3, 5.4) as stage 3? | Accepted, with the small driver changes |
| 7 | CodeView in both variants, the runtime stepped over by source path (9.3)? | Accepted, with the Options setting to step in |
| 8 | Debug `abort` with Microsoft's words, without the dialog (4.3)? | Accepted |
| 9 | The compiler fixes left to the compilers' owners? | **Changed:** made in this work as stage 1's first steps, each a separate commit under that compiler's gates. cpp11's three (then version 1.30); c90's stack probe and its -O0 setjmp bug; GRIDE's masm's instructions, checked against ml64. `FrameCheck` stays as a guard |
| 10 | The prototype as stage 1's starting point, or a fresh start? | The prototype, reorganised into the five libraries. What the design replaces (the write-through streams, a HeapAlloc per malloc, the per-call I/O) is rewritten per 10.2 and 10.3, not carried over |

---

## 18 Review record

**The review.** Draft 1 was reviewed independently on 2026-10-23 by a reader given only the document, the
requirements and the references:
- RTS6x;
- the prototype's sources;
- the compilers' headers and code generators;
- Shalimar's compiler and runtime;
- GRIDE's link, masm and build engine;
- Microsoft's vcruntime and UCRT sources.

Every finding was checked against the sources before it was taken in.

**Facts corrected:**

| Finding | Checked | Change |
|---|---|---|
| GRIDE's link already writes the TLS directory from `_tls_used` | `LINK\src\image.cpp:219-229` | 9.6: the runtime supplies the TLS data; "to be checked" dropped |
| cpp11 already emits a `thread_local` contract (`_tls_index`, `__tls_guard`, `__dyn_tls_on_demand_init`, `.CRT$XDU`, `/include:__dyn_tls_init`, `__tlregdtor`) | `C++Optimize\src\backend\Masm.cpp:734-736`, `X86_64Linux.cpp:436-447` | 2, 7 (step 5), 8.2, 9.5 |
| c90 never calls `__chkstk`; neither compiler calls `memcpy`/`memmove` implicitly | `Compiler-Ci\src\backend`: no `__chkstk` | 2 corrected; c90's missing stack probe added as a compiler fix and a build check (`FrameCheck`) |
| Microsoft's `abort` exits 3 only in Debug; Release fast-fails, 0xC0000409 | `ucrt\startup\abort.cpp:15-19, 71-89` | 4.3: behaviour per variant; 11.2/11.3: exit-code probes |
| The unwind functions are kernel32 exports on x64, so no ntdll import is needed | `dumpbin /exports kernel32.dll` | 8.4 |
| "RTS6x's stdio runs unchanged" contradicted the buffering design; RTS6x lacks `setvbuf`, `fgetpos`, `fsetpos` and `tmpnam`, and queries the position on every text fill | `Stream.h`, `fflush.cpp`, `StreamRead.cpp` | 10.3: Windows gets its own stream layer; RTS6x's formatting engines stay shared, and the C6747's stdio is untouched |
| `round` is no `roundsd` mode, and `sqrtsd` alone does not set errno | C99, the UCRT | 10.4 |
| GRIDE's masm lacks about 15 instructions the SSE2 work needs; `DB` bytes cannot carry relocations | `MASM\src\x64_encode.cpp:223` | 9.2: added to masm in stage 1 |
| CTRL+Z ends input only on files and pipes | `ucrt\lowio\read.cpp` | 6 |
| The UCRT's local time is `TZ`, then its own rule over `GetTimeZoneInformation` | `ucrt\time\tzset.cpp` | 6 |
| `printf` and its family come from `legacy_stdio_definitions.lib`, not UCRT exports | toolchain.json's hostCrt | 5.2 wording |

**Gaps filled:**
- C90 functions RTS6x lacks (`setvbuf`, `fgetpos`, `fsetpos`, `tmpnam`, `div`, `ldiv`, `strcoll`, `strxfrm`),
  the names cpp11's headers declare (`strtoll`, `strtof`, `strtold`, `system`), and shalimar's built-ins
  (`hypot`, `log2`, `cbrt`) are now in the components, the stages and a completeness test (11.8).
- LLP64: `%zu`, `%td` and `%I64d`, 64-bit positions, files past 2 GB, and Microsoft's limits on FILEs and
  descriptors (3, 6, 10.3).
- C++03-then-C++11 ordering inside the C++ headers, with the level in the catalogue (5.2).
- UTF-8 placed consistently as later; GRIDE's link has no resources for a manifest (1, 6, 16).
- scanf fast paths (10.3).
- The catalogue's per-target names and structured types; `Shortest` into RTS6x's sources (14.2, 14.4).
- Shalimar: `shm_fail` is new; name shadowing and the qualified form; returned `char *` and
  `size_t`/`long` results (14.3).
- SEH-to-signal mapping and Ctrl+C, `_matherr`, `quick_exit`, MXCSR left as Windows sets it (7); `/GS` and
  `/NODEFAULTLIB` for cl-built code (13.4, with the prototype's `mathx-cpp` failure as the case);
  `tmpfile` in the temp folder with delete-on-close, handle inheritance, `argv[0]` (6).

**Risks addressed:**
- **Threads:** a TLS-callback flag would race, because the creator may be inside an unlocked call when the
  new thread starts locking. So the flag is now set in the creating thread by a `CreateThread` wrapper, with
  the callback as a backstop. cppparse starts threads (9.6).
- **Layering:** draft 1's layers were cyclic (errno, the process's end). The layers are reordered with
  `grtwin` lowest and `grtstart` the root, and the check is "no name defined twice" (4.2, 12.3).
- **Exceptions** moved into stage 2, because cppparse and the toolchain demo throw (8.4, 16).
- **Buffering:** "console" now means `GetConsoleMode` succeeds, so NUL is fully buffered. The 4 KB buffer
  and the large-fwrite bypass are Microsoft's, so a crash leaves the same bytes. The deviation from "line
  buffering to a console" is recorded with its reasons (10.3, decision 3).
- **Step filter:** by source path, because a static runtime shares the program's module; callbacks still
  stop (9.3).
- **The allocator** is specified: the side table, a full region, the Debug layout and fill bytes (10.2).
- **Compiler prerequisites** are stage 1's entry criteria, including cpp11's keyword renaming of `div` and
  its `-g` bug (2, 16).
- **strcmp's** page-crossing (10.2).
- **One benchmark rule** (10.5).

**Not taken:**
- The finding that compilerpp throws. Its sources have the words only in strings and comments: 0
  try/throw/catch lines, against cppparse's 73 and the toolchain demo's 3.

**The design review and go-ahead** (2026-10-23, by the GRIDE3 session, on the user's behalf). Draft 2 was
accepted against every direction given for the runtime:
- machines without Visual Studio, with a Setup test;
- our toolchain only, Microsoft's sources as inspiration;
- efficiency, with the 5% rule;
- baseline first;
- the library and header organisation, legacy before modern, one C header set;
- the components;
- Shalimar's access, with the catalogue in the baseline;
- the review record.

Its decisions are section 17's. What it changed in this draft:
- **The compiler and tool fixes** are stage 1's first steps in this work, not the owners' (decision 9; sections 2,
  16).
- **RTS6x's two bugs** are fixed for the C6747 too, as a commit of their own checked by the C6747 examples, not
  only in `_WIN32` branches (sections 11.7, 15, 16).
- **Efficiency starts in stage 1:** the benchmarks are recorded there, and stage 1 is slower than the prototype
  on none of them. The 5% rule binds from stage 2's exit (sections 10.5, 16).
- **Wording:** section 13.3 names the GUI GRIDE.
