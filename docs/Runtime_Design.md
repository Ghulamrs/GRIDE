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
- Console programs (`main`) first; `wmain`, `WinMain` and `wWinMain` came after them (section 7). DLLs come
  later (section 16).
- The "C" locale only, with narrow characters in the ANSI code page, as the Microsoft CRT's "C" locale has
  them; a program may ask for UTF-8 instead (section 6). There are no languages.

---

## 2 What the compilers' code needs

The prototype compiled every C and C++ example for x86_64-windows with c90 and cpp11 (-O2, and -O0 -g) and
listed each object's undefined symbols. It listed shalimar's output and runtime the same way, and the
reviewer read the compilers' code generators. The names below are the runtime's fixed contract. Their names,
layouts and behaviour are the Microsoft ABI's, because both compilers emit Microsoft-ABI code and the same
objects must also link with Microsoft's CRT (section 13.2).

| Name | Emitted by | What it must do |
|---|---|---|
| `mainCRTStartup`, `wmainCRTStartup`, `WinMainCRTStartup`, `wWinMainCRTStartup` | link (the default entry, by the subsystem and the mains the objects define) | start-up and exit, section 7 |
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
| `grtstart` | the root: the four entries (`mainCRTStartup`, `wmainCRTStartup`, `WinMainCRTStartup`, `wWinMainCRTStartup`), the `.CRT$X*` markers, `__chkstk`, `_fltused`, `main`'s structured-exception filter, `__setusermatherr` | everything below, and the program's `main` |
| `grtcompat` | legacy and Microsoft-compatibility names: the 32-bit time functions old c90 objects call (`time`, `localtime` with a 32-bit `time_t`), POSIX names Microsoft keeps in `oldnames.lib` (`fileno`, `close`, ...), what cl-built objects need (`/GS`'s `__security_cookie`, `__security_check_cookie`, `__GSHandlerCheck`, `__report_rangecheckfailure`; later `__CxxFrameHandler4`) | `grtcxx`, `grtc`, `grtwin` |
| `grtcxx` | C++ support: `operator new`/`delete`, the new-handler, `_purecall`, type_info and RTTI, function-local statics, thread_local's dynamic initialisation and destructors, `std::exception`'s helpers, exceptions | `grtc`, `grtwin` |
| `grtc` | the ISO C library: C90 core and the C99/C11 additions (math, string, memory's front end, the formatting and scanning engines, stdlib, time's calendar, locale, signal, setjmp, assert), and the Windows stream layer | `grtwin` |
| `grtwin` | the Windows layer, the lowest: descriptors, the allocator's back end, errno's storage, the environment, argv, the clock and the time zone, the filing component's io.h/direct.h/stat functions, `CreateThread`'s wrapper (section 9.6), the TLS directory (`_tls_used`, `_tls_index`, the `.tls` markers, the callback array: in the lowest layer because errno's thread_local and grtcxx's read `_tls_index`), `system`, the process's end (`ExitProcess`, the fast-fail) | `kernel32` only |
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
| Release | `grtc.lib`, `grtcxx.lib`, ... | -O2 | no: under `-g` the compilers write slower code (9.3) | `abort`, a failed `assert`, an uncaught exception and `_purecall` end the program through the fast-fail (`int 29h`, FAST_FAIL_FATAL_APP_EXIT): exit 0xC0000409. The commonest invalid parameters Microsoft checks (a null `FILE *` or format) fast-fail the same way (FAST_FAIL_INVALID_ARG) |
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

### 5.1 Before stage 3

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
| the compilers' own search (no -I) | c90 and cpp11 look in `<exe>\include\c` ... before their compiled-in source path | a small change in each `Driver.cpp`, made in this work (decision 6); for one release Setup also installs the old `lib\*.h` and `include\*`, which an earlier installation's tools read |
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
  called, as the UCRT does in the "C" locale.
- **UTF-8.** `setlocale(LC_CTYPE, ".UTF-8")`, the UCRT's spelling, makes the program's narrow strings UTF-8, and
  `"C"` makes them what they were. Paths then convert from UTF-8, and a name that is not UTF-8 fails with EILSEQ.
  argv and the environment are spelled again in UTF-8. A text console is written and read in UTF-16 (`win\utf8.c`);
  files, pipes and a console in binary mode take the bytes as they are. A program that does not ask links none of
  this. Section 16 has the reasons, and what it leaves.
- **errno** comes from Windows' error numbers by the Microsoft runtime's mapping (written from its
  documented values).
- **The environment** is read once, narrow, with the `=C:` entries left out. `getenv` is a case-insensitive
  search, and its pointers stay valid for the program's life.
- **argv** comes from `GetCommandLineW` by Microsoft's rules. `argv[0]` is taken by quotes alone, with no
  backslash rule; the arguments take the rules for 2n backslashes before a quote and `""` inside quotes.
  An empty command line is taken as the program's path. Each argument is narrowed to the ANSI code page, or to UTF-8
  once the program asks, or kept in UTF-16 for a wide entry (section 7).
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

1. `mainCRTStartup`, or the entry the program has (below): the CPU's features (CPUID once: SSE4.1, ERMSB, AVX2 -
   section 10.2), the clock's origin, the standard descriptors, then the arguments, so that a static object can
   read `__argc`. MXCSR and the x87 control word are left as Windows sets them, as Microsoft's x64 CRT leaves them.
2. The C initialisers, `.CRT$XIA` .. `.CRT$XIZ`, null entries skipped. A non-zero answer ends the program
   with 255 and a message.
3. `__setusermatherr`, when the program defines `_matherr`.
4. The C++ initialisers, `.CRT$XCA` .. `.CRT$XCZ`. Each registers its object's destructor with `atexit`.
5. The main thread's `thread_local` initialisers. Windows calls the TLS callbacks (`.CRT$XL*`) before the
   entry point, and they run `.CRT$XDA` .. `.CRT$XDZ` for the thread, as `__dyn_tls_init` does.
6. envp, then `exit(main(argc, argv, envp))`, with `main` called inside a structured-exception
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

**The four entries** differ only in what they ask for at step 1 and pass at step 6, as Microsoft's four do. Each is a
member of its own, because each names the program's function, and a program defines only some of them:

| Entry | Calls | Arguments it asks for |
|---|---|---|
| `mainCRTStartup` | `main(argc, argv, envp)`, in the ANSI code page | `__argv` |
| `wmainCRTStartup` | `wmain(argc, argv, envp)`, in UTF-16 | `__wargv` |
| `WinMainCRTStartup` | `WinMain(image, 0, line, show)`, the line in the ANSI code page | `__argv` |
| `wWinMainCRTStartup` | `wWinMain(image, 0, line, show)`, the line in UTF-16 | `__wargv` |

`line` is the command line past the program's name, which ends at any character up to the space outside quotes, a
different rule from argv's. `show` is STARTUPINFO's `wShowWindow` where its `STARTF_USESHOWWINDOW` is set, else
`SW_SHOWDEFAULT`. A program that is not given argv reads `__argc` with `__argv` or `__wargv` from `<stdlib.h>`; the
other of the two is a null pointer. A failed `assert` in a windows program writes to stderr and ends as a console
program's does, as `abort` does under decision 8: Microsoft's runtime asks in a dialog there.

Link picks the entry and the subsystem from the mains the objects define, a library's not counting, as Microsoft's
link does. Without `/subsystem:` a `WinMain` or `wWinMain` with no `main` or `wmain` beside it is a windows program
and anything else a console one. Without `/entry:` each subsystem takes its narrow start-up unless only the wide name
is defined. So a program defining `main` and `wmain` starts in `main`, and `/subsystem:console` with only `WinMain`
fails, asking for `main`. GRIDE's engine passes `/subsystem:console` and then the project's link flags, where the last
`/subsystem:` wins, so a windows program sets `/subsystem:windows` there.

**A program without standard handles**, as a windows program started outside a console is: a handle that is null,
invalid or of no known type leaves its descriptor open in text mode, as the UCRT does, and its stream's descriptor -2,
which `_fileno` gives back. Output to it is lost and an error, and input from it ends.

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

### 8.4 Exceptions (in widening, before cppparse is an exit test; built 2026-10-30, section 16)
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
- The Debug variants are compiled with `-g`, so a Debug program's PDB carries the runtime's functions and
  lines and gride-dbg can show them and step into them. The Release variants are not: under `-g` cpp11
  inlines nothing (`inlineTarget` declines every site, "inline-site-debug-info") and c90 writes about a sixth
  more instructions (a comparison set into a register and tested again, more moves). Built with `-g` (e423505)
  the Release runtime took fgets from 0.96 to 1.24 of Microsoft's time, sscanf from 1.28 to 1.45 (integers) and
  1.21 to 1.36 (floats), and printf of floats from 1.45 to 1.59 (`bench\results.md`). The Release variants get
  CodeView back once both compilers write the same code with `-g` as without, as cl does with /Zi.
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
- **The TLS callback is the backstop** (`__grt_thread_attach`, `.CRT$XLB`; built 2026-10-30). It sets the
  flag in a thread the wrapper did not start, before that thread runs: one Windows starts itself (the thread
  pool, Ctrl+C's handler), or one a cl-built object starts through `__imp_CreateThread`. A call the creating
  thread began before the new thread's callback ran still finishes unlocked, so it can meet the new thread's
  first locked calls. A program whose threads come from the runtime's `CreateThread` has no such window.
- **The state of each thread is in static TLS**, the machinery cpp11's `thread_local` uses (built
  2026-10-30): errno (`win\errno.cpp`), strtok's place, gmtime's and localtime's result, asctime's text,
  rand's state, tmpnam's text, the SIGFPE, SIGILL and SIGSEGV handlers, and strtod's two big integers (RTS6x's
  `stdlib\FloatTextDecimal.cpp`, 2026-11-02). printf and scanf keep no statics. Each is zero-filled data that
  no code makes, so `grtc` and `grtwin` need nothing from `grtcxx` for it.
- **The TLS directory** is `grtwin`'s `win\tls.asm`, which `mainCRTStartup` links into every program; GRIDE's
  link writes the image's directory from `_tls_used` (`LINK\src\image.cpp`). It holds `_tls_used`,
  `_tls_index`, the template's markers (`_tls_start` in `.tls`, `_tls_end` in `.tls$ZZZ`) and the callback
  array from `__xl_a` (`.CRT$XLA`) to `__xl_z` (`.CRT$XLZ`): the backstop (`XLB`), then `grtcxx`'s
  `__dyn_tls_init` (`XLC`) and thread-local destructors (`XLD`), linked when a program has a `thread_local`
  (section 8.2).
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

  Under UTF-8 (section 6) the buffering is the same and the last step differs: `WriteConsoleW` takes at most 1,024
  bytes' worth at a time, so a longer call is several, and a character that the end of a write cuts in two, as a
  `putchar` loop cuts every one, waits at the descriptor for the next write.
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
- `toolchain\include\` holds the one C header set of 5.2 (`c\`, with `sys\`), its `_gride.h`, and the Windows and
  POSIX headers (`win\`). cc1's project copies them to `include\c` and `include\win` beside the programs, and cxx1's
  puts its C++ headers (`C++Optimize\include`) in `include\c++`.
- RTS6x's sources stay in `toolchain\TI\RTS6x\src`. Windows-only changes in them are `#if defined(_WIN32)`
  branches, kept to few; the C6747's code is compared before and after every change (section 11.7). The
  stream layer is grt's own (section 10.3).

### 12.3 MSBuild targets (`toolchain\msbuild\Toolchain.targets`)
- `WindowsLibrary`: sources through c90/cpp11 (-S -masm=masm) and masm, packed by `link /LIB`.
- `LibraryClosed`: the libraries together leave nothing undefined but the program's own entry (`main`, `wmain`,
  `WinMain` or `wWinMain`).
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

### 14.1 How Shalimar reaches its runtime
- shalimar emits calls to `shm_*` functions (`shm_print_int`, `shm_line`, `shm_array_make`, ...): plain C
  names, `extern "C"` in `runtime\shmrt.h`.
- For its built-ins it calls C library names (`Compiler-Si\src\Library.inc`, section 14.3: `sqrt`, `sin`, `pow`,
  `fmod`, `trunc`, `hypot`, `log2`, `cbrt`, `fabs`, ...), and `shm_c_<name>` where the C signature is not Shalimar's.
- The program defines `shm_user_main`, `shm_init_globals` and `shm_name_files`; the runtime's `main` calls
  them.
- A name not in the table is refused. Where its C signature needs a type Shalimar has no form for, the
  refusal says why (`Builtin.cpp`'s unborrowable list: pointers written through or into an argument, varargs).
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
- **A table generated from the catalogue and compiled into shalimar.** It replaces `Builtin.cpp`'s hand-written
  rows: each entry the catalogue marks usable from Shalimar becomes a built-in with its shape.
  - `Compiler-Si\src\Library.inc`, and the runtime's half, the folder `runtime\wrappers\`, are written by
    Toolchain.targets' ShalimarLibrary in shc's build. All of it is committed, since the macOS build compiles what is
    committed, so a file behind the catalogue is rewritten, or removed where the catalogue no longer asks for it, and
    fails the build once.
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
| a returned `char *` (`getenv`, `strerror`) | copied into a text the call passes last, cut at its capacity as an assignment is | a null pointer is the empty text, or a failure where the catalogue says so |
| a `size_t`, `long` or `long long` result | int, checked | a value past int's range is a failure, and so is a negative size going in |
| by-reference parameter | a pointer to the variable | out-parameters (`modf`, `frexp`) become two-result built-ins |
| errors | `errno` and a function's error result | the catalogue's mark says what ends the program. The wrapper checks it and calls the runtime's `shm_fail(message)` with "remove: No such file or directory"; with no rule, the C result is the answer (NaN from the maths, -1 from `getchar`) |

The catalogue's marks:
- `{"builtin": "sin", "shape": "Real"}`: a built-in called straight, as the first 24 always were.
- `{"usable": true}`: a Typed row. It is called the way a `uses` declaration is, so an array, a text and two outputs
  need nothing new from the code generator; two outputs come back through pointers after the inputs. `fail` is the
  rule: `nonzero` (`remove`, `rename`), `negative` (`putchar`, `puts`, `clock`, `time`) or `null` (`setlocale`). The
  catalogue's failure text cannot be the rule: "a negative or non-zero result" would end a `putchar`, whose answer is
  its character. `omit` gives a pointer parameter as null (`time`).
- Unmarked though the types map: what writes through a pointer it is given or answers one into its argument
  (`strcpy`, `strchr`), the Microsoft-level names and `quick_exit` (a built-in means the same on every host target),
  and `abs`, whose name is Shalimar's.
- A wrapper is `shm_c_<name>`, only where the C signature is not Shalimar's. The rest are called straight. Each is a
  file of its own, `runtime\wrappers\shm_c_<name>.cpp`, and so a library member of its own: a program links the
  wrappers it borrows and no others, with `Borrow.cpp` under those that use it.

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
Shalimar built-ins compile for the C6747, and one its target has not done is refused at the call. shmrt6x is built
over RTS6x as it is now, with the same wrappers. `remove` and `rename` say only "failed" there: the C$$IO$$ channel
answers them with a status and no reason, so `errno` has nothing to hold.

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
| later | wide characters; a DLL variant if wanted | |

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

**Exceptions, thread_local and the state of each thread (2026-10-30).**
- **Exceptions (8.4)** are `grtcxx`'s `cxx\exceptions.cpp`: `__CxxFrameHandler3` over the FuncInfo cpp11 writes (the state from the ip-to-state map, the try blocks, the handlers matched against the thrown CatchableTypes), and `_CxxThrowException`. A catch is reached by `RtlUnwindEx` with STATUS_UNWIND_CONSOLIDATE, as vcruntime reaches it. Rethrow, nested catches, `std::uncaught_exception(s)`, `terminate`/`set_terminate`, `unexpected` and an uncaught exception's end (the filter, `terminate`, `abort`: 0xC0000409) are there too. `win\seh.c` wraps the kernel32 calls and keeps each thread's block in a fiber-local slot. `cxx\ehcall.asm` holds three guarded calls with hand-written unwind data and language handlers: the catch funclet, a destructor while unwinding (a throw out of it calls `terminate`), and a catch object's copy constructor (a throw out of it calls `abort`). A `nop` follows each call, because a return address on the epilog would tell Windows' unwinder that the helper is already leaving, and it would not call the helper's handler. `bad_cast` and `bad_typeid` are thrown by `cxx\badcast.cpp`.
- **thread_local (8.2)**: the TLS directory is `grtwin`'s (`win\tls.asm`), not `grtstart`'s as 4.2 first had it, because the layers below the root read `_tls_index`; `mainCRTStartup` links it into every program. `grtcxx`'s `cxx\threadlocal.cpp` and `tlsdata.asm` hold `__dyn_tls_init` (the thread-attach callback that runs `.CRT$XDU`), `__dyn_tls_on_demand_init`, `__tls_guard` and `__tlregdtor`. As in the Microsoft runtime's start-up, the first thread's objects are made after the static objects and before `main` (`.CRT$XCY`). Each thread's destructors run when it ends (the thread-detach callback), and `exit` (`c\exit.cpp`) runs the calling thread's before the `atexit` list.
- **9.6's state for each thread** is in static TLS: errno (`win\errno.cpp`, which RTS6x's `ErrorNumber` reaches through `_errno` on Windows), strtok's place, gmtime's and localtime's result, asctime's text, and rand's state. `rand` is now `c\rand.cpp`, the Microsoft runtime's generator: RTS6x's is C's example, and its sequence was not the UCRT's. `asctime` pads a one-digit day with a space, as the UCRT does; stage 1 had it as two digits. The thread-attach callback (`__grt_thread_attach`) sets the threads flag for a thread the runtime did not start.
- The tests: cpp11's conformance suite on GRIDE's runtime (`tests\conformance.ps1`, the 792 valid cases at 1.43) gives Microsoft's verdicts on all but 38 at the two levels. Of those, 34 fail to link for want of C library names that are stage 2's: C99 math, `quick_exit`, `_Exit`, `llabs`/`lldiv`, mb*/wc*, `setvbuf`, `fgetpos`/`fsetpos`, `tmpnam`, the v*scanf trio, `strcoll`/`strxfrm`. One case prints a `%d` with no argument for it (`decl02_decltypeb`, reported to cpp11), and one was a timeout in Windows Error Reporting under load (both runtimes take 8.4 s there). `tests\exceptions\rethrow.cpp` (eight shapes of rethrow and nested catches) and `tests\perthread\perthread.cpp` (the state of each thread, and thread_local objects made and ended in order) are byte-identical to the UCRT's transcripts at -O2 and -O0. The earlier probes, `threads.ps1` (10 of 10), `filing.ps1`, the strings, sort and printf probes, and the completeness test are unchanged.

**The C library names the compilers' headers declare (2026-10-30).** The conformance suite's link failures named them; each was first probed on the UCRT, then built to give its transcript.
- **stdlib** (`c\stdlib99.cpp`, `c\exit.cpp`, since the UTF-8 work `c\multibyte.cpp` for the multibyte names): `llabs`, `ldiv`, `lldiv`, `atoll`; `mblen`, `mbtowc`, `wctomb`, `mbstowcs`, `wcstombs` in the "C" locale (a byte is the wide character of its value; above 0xFF, -1 and EILSEQ); `_Exit`, `quick_exit`, `at_quick_exit`. As on the UCRT, `quick_exit` and `_Exit` write no stream and run no `atexit` function, but the calling thread's thread_local objects are still destroyed by the TLS callback ExitProcess calls.
- **string and ctype** (`c\string99.cpp`): `isblank` (the UCRT's 0x40), `strncat`, `strcoll` and `strxfrm` in the "C" locale.
- **stdio**: `setvbuf` and `setbuf` (`c\setvbuf.cpp`). As the UCRT does, `_IOLBF` buffers fully, the size is rounded down to even, and a bad mode or size fails fast. A write of whole buffers now goes straight to Windows and the rest is kept, as the UCRT's `fwrite` does, so a program that ends without flushing leaves the same bytes on disk. `fgetpos`, `fsetpos`, `_fseeki64` and `_ftelli64` (`c\fpos.cpp`; files past 2 GB). `tmpnam` (`c\tmpnam.cpp`, per-thread buffer) and `tmpfile` take their names as the UCRT forms them: the temporary folder, `s` or `t`, the process id in base 36, a dot and a count in base 36 (`win\temporary.c`). `tmpfile` creates the file with `x` and `D`, the mode letters fopen now takes (C11's exclusive create, and the UCRT's delete-on-close), so Windows removes it however the program ends. `vfscanf`, `vscanf` and `vsscanf` (`c\vscanf.cpp`).
- **C99 math**: the exact functions are `c\mathexact.cpp` (copysign, fmax, fmin, fdim, nan, logb, ilogb, scalbn, scalbln, nextafter, nexttoward, lrint, llrint, lround, llround, remainder, remquo by exact long division) and `c\rint.asm` (rint, nearbyint). They give the UCRT's results bit for bit: its NaNs, its errno, `nan()` ignoring its text, `remainder`'s signed NaN for an infinite x where `remquo` gives the plain one, `llrint(2^63)` as LLONG_MIN. `fma` (`c\fma.asm`) is the processor's `vfmadd231sd` where CPUID shows FMA (`__grt_fma`, found with AVX in `c\memory.asm`). Elsewhere it is `c\fmasoft.cpp`, the exact sum in a 256-bit window, which agrees with the instruction on 3,000,000 random cases, subnormals and cancellations included. exp2, expm1, log1p, asinh, acosh, atanh, erf, erfc, tgamma and lgamma (`c\mathspecial.cpp`) are computed in double-double over RTS6x's kernels and rounded once. Against a 110-digit reference every result differing from the UCRT's is within half an ulp, and the UCRT's are off by up to 2.5 ulps (133 in one tgamma case), so GRIDE's runtime is the one correctly rounded there. tgamma also keeps the subnormal results the UCRT flushes to zero.
- The catalogue has 211 entries, all done for x86_64-windows. `_fseeki64` and `_ftelli64` stay out of it: no header of the compilers declares them.
- **Signals** (`c\signals.cpp`, which replaces RTS6x's three signal files on Windows) take the UCRT's numbers: SIGINT 2, SIGILL 4, SIGFPE 8, SIGSEGV 11, SIGTERM 15, SIGBREAK 21 and SIGABRT 22, with 6 a second name for SIGABRT. RTS6x's table held 16 and refused SIGABRT. Behaviour follows the UCRT:
  - `signal` answers 3 and 5 with SIG_ERR, and any other unknown number fails fast.
  - SIGFPE, SIGILL and SIGSEGV have a handler for each thread.
  - A handler is set back before it runs: to SIG_DFL, except that `raise` leaves SIGILL's and SIGSEGV's at a third value, as the UCRT's does.
  - `raise` with SIG_DFL ends the program with 3 and writes no stream.
  - Ctrl+C and Ctrl+Break reach their handlers through a console control handler (`win\control.c`).
  - An access violation, an illegal instruction or a floating-point exception in the first thread reaches SIGSEGV's, SIGILL's or SIGFPE's handler through main's language handler. `start\mainguard.asm` calls main with unwind data naming `__grt_main_handler` (start.c), the UCRT start-up's filter. An integer division by zero is not a signal there, and is not one here.
  - `abort` still reaches SIGABRT's handler, through `SignalTable::deliver`.
- **The terminate and unexpected handlers** read back, before a program sets one, as the UCRT's do: a handler that aborts, and `terminate`.
- **exception_ptr**, which cpp11 1.44's `<exception>` builds on libcpmt's nine `__ExceptionPtr` functions in cl's two-word layout, is `cxx\exceptions.cpp`'s:
  - `current_exception` copies the object of the catch that is running into a counted block, using its exact type's copy constructor;
  - `rethrow_exception` raises the block's object again;
  - a catch that ends leaves that object to the block, which destroys it when the last `exception_ptr` goes.
  - `throw_with_nested` and `rethrow_if_nested` work over them.
- **exp, log, sin, cos and pow** (10.4) are `c\mathfast.asm`, written with its tables by `tools\mathfast.py`.
  - exp: Tang's 2^(j/128) table.
  - log: 128 buckets, each with an 8-bit reciprocal so that z·invc − 1 is exact, and the large terms summed with their errors.
  - sin and cos: Cody-Waite reduction by π/2 in three parts, and Taylor kernels in two chains.
  - pow: log's double-double times y, then exp's kernel.
  - Where CPUID shows FMA, each takes a fused path, written as VEX bytes because GRIDE's masm has no FMA mnemonics.
  - Everything outside the common case goes to RTS6x's kernels (`c\mathslow.cpp`): specials, range errors, subnormals, huge trigonometric arguments, x ≤ 0 in pow.

  The UCRT probes are unchanged: mathlogpow byte-identical, mathvec within 1 ulp, and `math99.ps1` passes. In a harness of 20,000,000 points against the UCRT, every result is within 1 ulp: exp 99.5 % identical, log 99.999 %, sin and cos 99.4 %, pow 99.9 %. Clearing k's register before `cvtsi2sd` took log from 2.4 to about 1.3 times Microsoft's time; the register would otherwise chain one call to the next.
- **A cpp11 catch funclet stores a fifth argument over its own saved rbp**: it reserves only the 32 bytes of home. `__grt_call_catch` now keeps rbp itself, as vcruntime's helper does (UWOP_PUSH_NONVOL in its unwind data), so the funclet's loss does not reach the frames above it. The bug was reported to cpp11 with a reduced case.
- The tests: `tests\names\names.cpp`, `quickexit.cpp` and `exitnow.cpp` are byte-identical to the UCRT's transcripts at -O2 and -O0, as are `tests\signals\signals.cpp`, `badsignal.cpp`, `default.cpp`, `fault.cpp`, `divide.cpp` and `ctrlc.cpp`. The last sends its own console Ctrl+C, and `probe.ps1 -OwnConsole` runs it in a hidden console of its own. `tests\math99\math99.ps1` runs 10,766 lines of special and spread values. The exact functions must match the UCRT's line exactly. The others must be within 1 ulp of it, or be one of the 159 lines `math99.judged` names, where the reference shows the UCRT more than an ulp out and GRIDE's runtime within half of one.

**9.6 finished (2026-11-02).** 1a408c7 had built the TLS directory, thread_local, each thread's state and the backstop. One piece of 9.6's state was still shared.
- **strtod's long way** (RTS6x's `stdlib\FloatTextDecimal.cpp`, for more than 19 digits or an inexact short way) kept its two 516-byte integers in static storage, as the C6747's one thread allows. Listing every symbol the five layers define in a writable section found it. The rest is the locked tables, flags and tables set once, and what the UCRT also keeps for the whole process. Six threads converting long decimals at once got 19,404 of 24,000 results wrong; the UCRT got none. On Windows the integers are now `thread_local`: zero-filled static TLS that no code makes, so `grtc` still needs nothing from `grtcxx`. Since then, none of 30 runs has a wrong result.
- **A thread started through `__imp_CreateThread` itself**, as a cl-built object starts one, is seen by the backstop. Six such threads write whole lines to one shared `FILE` (2,400 `fprintf` and 2,400 `fputs` lines, none torn), each with its own errno, strtok place and thread_local, made and ended in each. The transcript matches the UCRT's in 31 runs of 31 at -O2 and at -O0. The one window the backstop leaves is in 9.6.
- The tests: `tests\perthread` matches the UCRT at -O2, -O0 and on the Debug variants; `threads.ps1` 30 of 30; `filing.ps1` 198 of 198 lines on each runtime; the completeness test 205 of 205 names; stage 2's probes are unchanged. cppparse, sampled at its latest commit and linked by GRIDEd's engine, runs `--health .` with its workers identical to the msvc build in 30 of 30 runs in Release and 30 of 30 in Debug.
- The benchmarks (`bench\results.md`) were run with the programs alternating, because the machine was loaded. The per-thread state costs nothing measurable: grt at 1a408c7's parent and at 1a408c7 give the same ratios on printf, sscanf, the stream rows, malloc and hello's start. The strtod change costs nothing against main's libraries. fgets went from 0.99 to 1.25 of Microsoft's time, and sscanf from 1.29 to 1.45 (integers) and 1.20 to 1.35 (floats). That came later, in e423505, which also put `-g` in the Release flags. **That was the cause** (2026-11-02): under `-g` cpp11 inlines nothing and c90 writes about a sixth more instructions (9.3). The Release variants are compiled without `-g` again, and the three ratios are back at a810798's (`bench\results.md`).

**The allocator of 10.2 (2026-11-03).** `win\heap.c`'s Release half is 10.2's allocator, where it was a wrapper over `HeapAlloc`.
- A block of up to 4 KB is one of 28 size classes. It comes from a 256 MB region reserved with `VirtualAlloc` and committed in 64 KB slabs, each slab carved into one class's blocks. A side table names each slab's class, so a block has no header.
- Each class keeps a free list and the slab it is carving, under a lock of its own once the program has a second thread; a new slab is taken under one more lock, always after the class's.
- Up to 16 regions are reserved. A larger block, a small one once they are full, and so any pointer outside them, is the process heap's. `realloc` keeps a block within its class and moves it otherwise; `calloc` zeroes what it hands out.
- The Debug variant keeps its header-and-guards layout (4.3).
- malloc-small went from 1.07 to about 0.5 of Microsoft's time, malloc-mixed from 1.07 to about 0.4 (best of 9, alternately, on a loaded machine).
- The tests: `threads.ps1` 30 of 30; a probe of eight threads making, checking and freeing 3.2 million blocks of every class and larger, half of them freed by another thread, with realloc's kept bytes and calloc's zeros checked, finds none wrong at -O2 and -O0 on either runtime; the probes, `debug.ps1`, `filing.ps1` (198 of 198) and the completeness test (205 of 205) are unchanged. cppparse's sample at its latest commit (b6d61c4) does not build with GRIDE's toolchain on either runtime: it declares `volatile LONG next`, and cpp11's `windows.h` declares `InterlockedIncrement(LONG *)` where the SDK has `LONG volatile *`.

**The benchmarks within 5 % (2026-11-03).** Each row over 1.05 was timed a stage at a time (sprintf alone, fprintf to a text and a binary file, the float digits alone) until its cause showed. Most were cpp11's code, which grt now works around in its own sources (the items are in the shared notes for cpp11's owner):
- **printf's integers** (1.49 to 0.92): cpp11 divides by a constant with `div`, two 64-bit ones per pair of digits. `decimalDigits` (`c\fmt\Print.cpp`) multiplies by the reciprocal below 2^32 and divides once per pair above it. cpp11 also writes an `idiv` by 1 for each difference of char pointers, four per `sprintf` in RTS6x's `OutputSink` and four per line in the text stream's `put`. `grt::bytesBetween` (`internal\grt.h`) subtracts the two as integers instead. sprintf and snprintf write through grt's own `MemoryOut`, which counts its room, and RTS6x's sink is left as it is.
- **printf's floats** (1.46 to 0.40): RTS6x's exact digits make every digit of the binary value, about 450 ns a value. `quickFloat` takes f, F, e, E, g and G of a finite value whose scaled digits fit in 64 bits: m x 10^k x 2^e is formed exactly in 128 bits and rounded to nearest, ties to even, as the exact digits round. Everything else (inf, NaN, %a, subnormals in e and g, a precision past 17 or 19, larger exponents) still goes to RTS6x's formatter. Against it, 28,536,480 conversions are the same: random doubles, every exact tie j/2^r, the powers of ten and their neighbours, at 0 to 20 places, with and without #. RTS6x's `DecimalDigits` divides by 10,000 and 10 through reciprocals on Windows too; the C6747's code is unchanged.
- **qsort** (1.30 to 0.93): cpp11 kept lo, hi, up, down and the comparison in memory around every call of the comparison. Elements of 4 and 8 bytes go to `c\qsortword.asm` (written by `tools\qsortasm.py`): the same steps with every pointer in a register. For 4- and 8-byte keys of 1 to 32 bits, 84 arrays of 0 to 200,000 elements end in the same order as on the UCRT, after the same number of comparisons. A comparison that throws unwinds through the routine to its catch.
- **memcpy at 64 to 256 bytes** (1.09 to 0.64): where Windows has AVX, 32-byte moves from both ends; memcpy and memmove of every size to 300 at every pair of alignments to 63 match a byte loop.
- The table is in `bench\results.md`. Other sessions' builds loaded the machine on and off, so each row was taken while it was quiet; under load a few short rows read up to 1.10, with Microsoft's times inflated as much. The probes, `threads.ps1` (30 of 30), the heap stress, `debug.ps1`, `filing.ps1`, the completeness test and math99 are unchanged.

**Step into GRIDE's runtime (9.3, 2026-11-03).**
- gride-dbg's session filter in toolchain.json gains the runtime's source paths: `*\toolchain\Windows\RT\*` and `*\toolchain\TI\RTS6x\src\*` (where the PDB says they were built), `*\src\runtime\RT\*` and `*\src\runtime\RTS6x\*` (where Setup installs them). A step into printf in a Debug program then steps over it, as over Microsoft's CRT.
- Tools > Options > Projects > GRIDE Paths, **Step into GRIDE's runtime** (settings.json `stepIntoRuntime`, off by default), has GRIDE send the filter without its path patterns, and the step stops in printf.cpp.
- gride-dbg's source-path map: a runtime source the PDB names under its build machine's `toolchain\Windows\RT\` or `toolchain\TI\RTS6x\src\` that is not here is taken from `src\runtime\RT\` or `src\runtime\RTS6x\` beside gride-dbg. build-setup.ps1 installs the two trees (the sources alone, no tests or benchmarks), and test-setup.ps1 checks them.
- Tested on a Debug program with a function whose source lies under a stand-in `toolchain\Windows\RT\`. With the filter, a step into it and into printf goes to the next line; without the path patterns it stops in the function and in printf.cpp(10). With the stand-in moved away and installed under `src\runtime\RT\`, the step stops in the installed copy.

**/DEFAULTLIB in the automatic choice (13.1, 2026-11-03).** With the Windows C runtime automatic, a link now takes Microsoft's where Visual Studio is found and either cl's objects are among its inputs or one of its libraries names Microsoft's runtime by `/DEFAULTLIB`. The libraries are the configuration's, found as the link finds them, plus the .pro's and the shared ones. The engine reads each one's directives from its bytes, as link reads them, for libcmt, msvcrt, libucrt, ucrt, libvcruntime, vcruntime, libcpmt and msvcprt, each with or without its d; the Output pane says which runtime was taken and why. The third-party example (`examples\projects\thirdparty-mathx`, whose mathx.lib names LIBCMT and OLDNAMES) builds that way on Microsoft's runtime with Visual Studio present. With GRIDE's runtime chosen it builds with Microsoft's libraries refused (13.4). Both programs print the same.

**The names cpp11 1.53's headers declare (2026-11-03).** cpp11 1.53 (Phase 7c) brought `<wchar.h>`, `<wctype.h>`, `<uchar.h>`, `<inttypes.h>` and `<fenv.h>`, with `<chrono>`, `<random>` and `<system_error>` over them. Its conformance suite then failed to link four cases on GRIDE's runtime. The 90 functions those headers declare are now grt's; the catalogue has 301 entries. Each was probed on the UCRT first (`tests\wide\wide.cpp`, whose transcript the runtime gives back byte for byte at -O2 and -O0), and the UCRT's own sources were read for the behaviour where a probe left it open.
- **Wide strings and conversions** (`c\wide.cpp`). The wcs* functions work on 16-bit units; `wcsxfrm` copies as `wcsncpy` does. `wcstol` and the other wide conversions read what the narrow ones read, with the white space `iswspace` sees and the digits of the 17 scripts the UCRT takes besides ASCII's. In the "C" locale a byte is the wide character of its value, and `wctob` answers 0x80-0xFF as a signed char, as the UCRT does. inttypes' six functions are here too.
- **uchar** reads and writes UTF-8 whatever the locale, as the UCRT's does: surrogate pairs through the state, and EILSEQ for surrogates, overlong forms and anything past 0x10FFFF.
- **wctype** (`c\wctype.cpp`). Windows' CT_CTYPE1 classes (GetStringTypeW, through `win\chartype.c`), whose bits are the UCRT's masks. Below 256 the UCRT's own table leaves the blank bit off tab and no-break space, and `iswblank` takes tab by name. Case mapping is ASCII's.
- **fenv** (`c\fenv.cpp`, `c\mxcsr.asm`). On x64 the UCRT reads and sets MXCSR alone, and so does grt; an x87 probe shows the x87 unit is neither read nor set. The flags and controls are its abstract bits written twice, `(a << 24) | a`; the control's high copy holds the rounding bits shifted right by 2.
- **Wide streams** (`c\wstdio.cpp`). In text mode a wide character is the byte of its value: `fputwc` of one past 0xFF is WEOF with EILSEQ, and the wide printf writes `?` for it. In binary mode it is two bytes. The wprintf family runs on the printf engine, now a template over the format's character type (`Printer.h`), with the UCRT's legacy wide meanings: `%s` and `%c` wide, `%S` and `%C` narrow. `swprintf` answers -1 for a text that does not fit. The wscanf family runs on RTS6x's scanner over a wide input source (`InputSource`, Windows only), with the format's conversions given their lengths. Its known limits: a format's own characters past 0xFF, and the non-ASCII digits and spaces the UCRT's wide scanf also takes.
- **rand_s** (`win\random.c`) reads advapi32's RtlGenRandom, the UCRT's generator, loaded on first use so that a program still imports KERNEL32.dll alone. kernel32.lib gains GetSystemTimePreciseAsFileTime and FormatMessageA, which cpp11's `<chrono>` and `<system_error>` call, and GetStringTypeW, LoadLibraryW and GetProcAddress for grt's own use.
- cpp11 1.54's `<mutex>`, `<condition_variable>` and `<thread>` call kernel32 directly, and four of the functions were not in `kernel32.def`: TryAcquireSRWLockExclusive, WakeConditionVariable, SwitchToThread and GetActiveProcessorCount (186cd68).

**Stage 2's exit test (2026-11-03).** On cpp11 1.54's release build, with GRIDE's runtime as main builds it:
- **Both example sets**: 180 of 180 cases are the same on GRIDE's runtime (LIB and INCLUDE empty) and on Microsoft's. The 60 programs built print what they printed on 2026-10-30, and on GRIDE's runtime each imports KERNEL32.dll alone. The 120 error cases stop at the line the error was put in. compilerpp's Release build, which failed on 2026-10-30, builds.
- **The debugging exercise**, replayed on gride-dbg: 80 moves, 70 stops and 10 runs to the end on each runtime, all 80 as the record.
- **The probes**: the UCRT probes are byte-identical at -O2 and -O0, with mathvec's 15 rows within 1 ulp, and so is `tests\wide`. `threads.ps1` passes 30 of 30, `debug.ps1` passes, `filing.ps1` gives 198 of 198 lines, and the completeness test links 295 of 295 names.
- **cpp11's conformance suite at 1.54** on GRIDE's runtime: 958 of 971 at each level. The differences that reach the runtime are decl02 (the case's own undefined behaviour) and cmath_c99_names (the UCRT an ulp off). lt01 (threads, mutexes, atomics and futures) links and runs as expected; the runner reads it as a crash, which is cpp11's own when the output file's name is long.
- **The toolchain demo**: all eleven programs ran, and the three examples print the same.
- **The Setup test**: Black, Grey and White, every check passed.
- **cppparse**, sampled at its latest commit (b6d61c4), is not part of this run. cpp11 1.54 crashes compiling it on either runtime: `backend\X86.cpp` at -O2, `analyzer\Compiler.cpp` at `-g`. The findings are in the shared notes for cpp11's owner. Its last pass was on 2026-11-02, 30 of 30 `--health` runs with workers, identical to the msvc build in Release and Debug.
- **The benchmarks**, every row of bench.c again, taken only while the machine was quiet: none is above 1.05 of Microsoft's time. The closest are sin (1.045) and log (1.04), and printf's integers are at 0.92, its floats at 0.41, the allocator's two rows at 0.48 and 0.38 (`bench\results.md`).

**Stage 3: the layout (2026-11-03).** Section 5's tree, built by the solutions and installed by Setup.
- **One C header set** (5.2) in `toolchain\include`. cpp11's C headers moved there with their history (`c\`, `c\sys\`, `win\`), and c90's sixteen were folded into them, so `Compiler-Ci\lib` and `C++Optimize\lib` are gone. Each header gives C90 first. The C99 and C11 library is behind `_GRIDE_C99_LIBRARY` (`c\_gride.h`: C++, `__STDC_VERSION__`, or a project's `_GRIDE_C99`), and `extern "C"` is behind `__cplusplus`. A function a header defines is `_GRIDE_INLINE`, a static function under c90, which has no inline. C keeps its `wchar_t` typedef, and `_fileno` is Windows', `fileno` POSIX's. c90 gets `time_t` of 64 bits on Windows (decision 4): `time()` and its five siblings are macros over `_time64` and the like, which also makes them link with Microsoft's CRT. grtcompat's 32-bit names stay for objects compiled before.
- **Checked**: every header preprocessed by cpp11 for x86_64-windows and tms6747 gives the text it gave before, except five bodies rewritten as C (a loop variable declared first, a C cast for `const_cast`). A probe of every C90 name compiled by c90 gives the same assembly for tms6747. For Windows the only differences are the calls to `_time64` and the like, and the frame offsets the wider `time_t` moves. The C99 headers and the Windows ones compile under c90, with and without `_GRIDE_C99`.
- **The tree** (5.3) beside the programs: `include\c`, `include\c++`, `include\win`; `lib\x86_64-windows` (grt, kernel32.lib, `shmrt[d].lib` over grt, `msvc\shmrt[d].lib` over Microsoft's); `lib\tms6747` (RTS6x, printf6x, shmrt6x, `shmrt\` and `shmrt-debug\`); `share\ccs`. The earlier header folders, `lib\*.h` and the flat `include\`, stay for one release. Each project deletes its libraries' earlier copies, so nothing finds a stale one.
- **The migration** (5.4): toolchain.json's `installIncludeDirs`, library entries, assembly directories and `definitionsDir`, in GRIDEd and GRIDE 2.0. GRIDEd's engine resolves a name other than `lib` and `include` under the installation. GRIDE 1.0's own lookups take the tree first, as do c90's and cpp11's header search, cpp11's search for RTS6x and shalimar's for its default runtime. Each falls back to the earlier layout. Setup, its test (which counts each folder), the toolchain demo and grt's test scripts use the tree as well.
- **The exit test**, on cpp11 1.56 (main's at the time), each part against main just before stage 3, built in a worktree of its own:
  - **The C6747**: RTS6x's Release libraries and shmrt6x(d) are byte-identical. All 610 of RTS6x's assembly files and the Shalimar runtime's 13 are identical once paths are set aside. The Debug libraries differ only in the header paths their line tables carry. The examples Hello (c90), sieve (c90) and shapes (cpp11), Debug and Release, give the same assembly and print the same on sim6747 and vm6747.
  - **Setup test**: Black, Grey and White, every check passed, the new tree's included.
  - **The probes**: `threads.ps1` 30 of 30, `debug.ps1`, `filing.ps1` 198 of 198, the completeness test 295 of 295, the UCRT probes and `tests\wide`, as in stage 2.
  - **cpp11's conformance suite at 1.56** on GRIDE's runtime: 975 of 989 at each level. The verdicts that differ from Microsoft's are decl02's and cmath_c99_names', as before.
  - **The toolchain demo** ran all eleven programs. **The debugging exercise** on GRIDE's runtime gives all 80 moves as the record.
  - **Both example sets**: all 180 cases give the same result with and without stage 3, on GRIDE's runtime and on Microsoft's: built or not, the error and its line, each program's output, exit code and imports.
  - What fails is cpp11 1.55's and 1.56's, the same without stage 3 (shared notes, for cpp11's owner). 1.56 crashes on compilerpp's `Semantic.cpp`. The `<ostream>` of 1.55 does not compile for tms6747, so the CCS examples Sample and SampleExt stop there. Its sentry calls `std::uncaught_exception`, which Microsoft's static runtime keeps in libcpmt.lib, a library GRIDE's link with Microsoft's CRT does not name. solve3-cpp11 and the probes' C++ therefore do not link on Microsoft's runtime; on GRIDE's they do.
  - **Found on the way**: C90's `div` is declared for C now, but neither grt nor RTS6x defines it, and c90 on Windows cannot call it, since its masm backend refuses an import of that name. (Done in stage 4, below.)

**Stage 4: Shalimar's direct access (2026-11-04).** Section 14, built in the order it was planned, each step a commit:
- `shm_fail` is public, and shortest float formatting moved down: `_gride_shortest_double` in RTS6x's sources, in `rts6x.lib` and grtc; the cl-built runtime over Microsoft's CRT compiles the same file and vm6747 includes it.
- shalimar's table comes from the catalogue (`src\Library.inc`), and the C6747's spellings of `trunc` and `round` with it. A file behind the catalogue fails the build.
- `c.name` always reaches the runtime. A program's own function still shadows the plain name.
- 105 of the catalogue's 307 entries are marked, for 106 library built-ins beside Shalimar's three, through 33 generated wrappers (`runtime\Wrappers.cpp`) over a few hand-written helpers (`runtime\Borrow.cpp`). The rest are called straight.
- On the C6747 the same rows hold; one its target has not done is refused at the call. vm6747 gained `rename` and RTS6x's `strerror` words.
- **C90's `div`**, stage 3's find. c90's MASM spelling gives the word up with NOKEYWORD as cpp11's does, and in such a file writes a `div` instruction as its bytes. `div`, `ldiv` and `lldiv` are RTS6x's now, and so grtc's, one definition of each (`c\stdlib99.cpp` gave up its two), and vm6747 answers them. `tests\names\divide.c`, which also divides unsigned 32- and 64-bit numbers, matches the UCRT's transcript on both runtimes at -O2 and -O0 and with the debug libraries, and prints the same on sim6747 and vm6747; `lldiv`, whose answer comes back through memory there, does too.
- **The exit test**, first on cpp11 1.59, then again on 1.60, main's at the end, with `div` and Microsoft's `libcpmt` (below) in. These are 1.60's figures, the build checks clean:
  - **`tests\shalimar\access.ps1`**: 61 of 61, over the release runtimes and over the debug ones. Each shape, each failure rule, shadowing and `c.name` run four ways (x86_64-windows over GRIDE's runtime and Microsoft's, tms6747 under sim6747 and vm6747), what the C6747 lacks on the first two, and what must be refused on the targets it concerns. The transcripts were read against worked values, not only against each other.
  - **Every `.shl` under `examples`**, -S with and without --debug on all four targets, 200 runs: assembly byte for byte as stage 3's compiler wrote it. So the table changed no code.
  - **The completeness test**: 301 of 301. **The toolchain demo**: Release and Debug.
  - **Both example sets**, against the run before step 2 on cpp11 1.57: 174 of 180 the same on GRIDE's runtime, 172 on Microsoft's, and every difference a gain. compilerpp's six: 1.57 crashed on them, and it now compiles, its syntax and semantic errors at their lines, and builds on both runtimes. solve3-cpp11's two, on Microsoft's: they had stopped linking, and link now. An earlier run's change in mathx-cpp was the harness's: .NET's redirected stdin began with a byte-order mark under a UTF-8 console. With stdin left alone it prints what it always did.
- **Open:** The macOS build's Makefiles are not in this tree, nor anywhere on the machine it was built on: whoever keeps them needs RTS6x's `_gride_shortest_double.cpp`, `Borrow.cpp` and every file of `runtime\wrappers\` in shmrt for the host targets, and `Shortest.cpp` out. (A borrowed function no longer costs all 33 wrappers at the link: see "One member to a wrapper" below.) The C6747's runtime has not done 161 of the 307 entries, 123 of them C99's, and the 39 built-ins among them are refused there. Everything the catalogue calls done for it was checked against `rts6x.lib`'s symbols, and six it had under-reported (`strtoll` and its like, `vsnprintf`, `wprintf`) are marked done.

**Microsoft's line takes libcpmt (2026-11-04).** cpp11's headers ask Microsoft's static C++ library for `std::uncaught_exception`, which every `<ostream>` sentry calls, for the nine `__ExceptionPtr` functions and for `set_new_handler` and `get_new_handler`. cpp11's own driver and its conformance runner link it; toolchain.json's `hostCrt` did not, so on Microsoft's runtime no program writing to `std::cout` linked through GRIDE's engine, and none using `exception_ptr` or the new handler.
- `<exception>` could have answered `uncaught_exception` from vcruntime's `__uncaught_exception`, as it does `terminate`, but that leaves the other eleven names unresolved. So the line changes, in GRIDEd's and GRIDE 2.0's toolchain.json and in GRIDE 1.0's `microsoftCrt`, and the headers do not.
- `libcpmt[d]` comes last, so that a name libucrt or libvcruntime answers today is still theirs: a program that linked before takes nothing from it.
- **Checked**, on cpp11 1.59, with GRIDEd's toolchain.json (GRIDE 2.0 and GRIDE 1.0 were not built):
  - **Both example sets**: 180 of 180 the same on the two runtimes, and all 60 programs built. Against stage 4's run four cases changed, on Microsoft's alone: compilerpp and solve3-cpp11 as they are, Debug and Release, which now build.
  - **compilerpp** as its .pro runs it, `-run sample.cpp`: exit 0 and the same 96 bytes, Debug and Release, on both runtimes.
  - **Programs that did not need it**: c-bank, cpp-shapes, cpp-table, cpp-vector3 and cpp-inventory linked with the old line and the new are the same bytes in Release. c-bank and cpp-shapes in Debug differ only in the link's time stamp.
  - **`exception_ptr` and `set_new_handler`**: probes that stopped at the link link and run, Debug and Release.
  - **cpp11's gate**, no file of cpp11's changed: 1,019 valid cases pass at each level, all 878 negatives are refused, census 1,766 of 1,769, as 1.59 froze them. One link and three census probes reached their 60 seconds beside other builds; each passed when run again.

**The C6747's built-ins (2026-11-04).** Stage 4 left 39 of Shalimar's built-ins refused on tms6747. 34 are RTS6x's now, one file per function, and vm6747 answers them; the C6747's runtime has not done 127 of the 307 entries, 91 of them C99's.
- **Shared, not written twice.** grt's C99 functions already stood on RTS6x's kernels, so they moved down whole and grtc takes them back through `rts6x-c.sources`' wildcards: `c\mathspecial.cpp` is gone, and `c\mathexact.cpp` keeps `nexttoward` alone. Their parts became members of the classes RTS6x has (`IntegralPart`, `Remainder`, `BinaryScale`, `Logarithm`, `ExpKernel`, `SquareRoot`, `DoubleDouble`) and of three new ones (`ErrorFunction`, `GammaFunction`, `FusedMultiply`). grt keeps three of its own: `rint` and `nearbyint` (`c\rint.asm`, which follows the processor's rounding mode) and `fma` (`c\fma.asm`, the instruction where there is one, over RTS6x's exact sum through `c\fmasoft.cpp`). RTS6x's `rint` and `nearbyint` are new, on the bits: to nearest, ties to even, the only direction it has.
- **Five left out, each with its reason in the catalogue's `why`, and each still refused at the call.** `system`: the C6747 has no command processor, and the C$$IO$$ channel no request that reaches one. `feclearexcept`, `fegetround`, `fesetround` and `fetestexcept`: the chip does have rounding modes and flags (FADCR, FAUCR, FMCR), but they cannot speak for a program's arithmetic here. The C6000 ABI's helpers, which divide and convert, round to nearest and raise no exceptions (SPRAB89B 8.1), and sim6747 and vm6747 model neither the modes nor the flags, so `fesetround` would change nothing and `fetestexcept` would answer 0 after a division by zero. `<fenv.h>` already refuses the target; a refusal says so, where a silent 0 would not. Giving the C6747 an environment would start with modelling those registers in sim6747.
- **What the C6747's floating point changes.** It reads a subnormal as zero, so the shared code tests a zero, a sign and a direction on the bits where that matters (`nextafter`, `tgamma`, `lgamma`). `llrint` and `llround` name 2^63's answer, since a conversion past the range is each target's own and the C6000 ABI's saturates. `isblank` answers 0x40 there too, so a program prints the same number on every target.
- **The exit test**, on cpp11 1.60:
  - **`rts6x.lib` and `rts6xd.lib`** define all 34, and every entry the catalogue calls done for tms6747 (180) is in both.
  - **`tests\shalimar\access.ps1`**: 76 of 76, over the release runtimes and over the debug ones. `c99` and `range` moved from `hosts\` to `all\` with the hosts' transcripts unchanged, and run four ways. `special` and `quick` are new and call what `c99` does not; their transcripts were read against worked values. `hosts\` holds `fenv`, and `refused\` holds `fenv` and `system` where it held `fma`.
  - **`math99.cpp`, `nexttoward` aside, built for tms6747** (a check of this work, not a committed test), under sim6747, -O2 over `rts6x.lib` and -O0 over `rts6xd.lib`. NaNs aside, whose pattern is each machine's, 10,523 of the 10,529 lines are grt's transcript bit for bit, and 10,525 pass math99's rule against the UCRT's. The rest are subnormals: `erf` of three, which the chip reads as zero, one of them past the rule, and three of `fma3`, whose argument `y * 1e-17` the program's own multiplication flushes. Under vm6747, whose answers are the host's library, every line passes the rule.
  - **grt, the code having moved**: `math99.ps1` at -O2 and -O0, the completeness test 301 of 301, and the names, strings, wide, printf and sort probes all as they were. cpp11's conformance suite passes 1,032 of 1,034 at each level; the two that differ from Microsoft's are `decl02` and `cmath_c99_names`, as before.
  - **The rest, on cpp11 1.61 (2026-11-05)**: the access tests again 76 of 76, release and debug, and the assembly of all 200 runs as stage 3's shalimar wrote it. Every probe at -O2 and -O0, threads 30 of 30, debug, filing and `math99`. `mathvec` is byte for byte what it was before this work, 1 ulp from the UCRT on `cos`, `cosh` and the like. Both example sets are 180 of 180 as stage 4's run and the same on both runtimes, and the debugging exercise is 80 of 80 on each. The toolchain demo ran, Release and Debug, GRIDE 2.0 and 1.0 built, and the Setup test passes for all three outlooks. Real hardware has not run any of it.
- `_Exit` writes no stream, so on the hosts a program's text still in the buffer of a piped stdout is lost, as `tests\names\exitnow.cpp` records. On the C6747 it has already gone out, under both emulators. `quick` prints nothing before it, so its transcript is the same four ways.

**UTF-8 console and paths (2026-11-05).** The row left two ways open, resources in GRIDE's link for a manifest or a runtime switch. It is the switch: a program asks with `setlocale(LC_CTYPE, ".UTF-8")`, and one that does not ask runs as it did.
- **Why not the manifest.**
  - It does half the work. `activeCodePage` changes what `CP_ACP` and the `A` functions mean, and leaves the console's code page alone, so a program printing UTF-8 to a console still needs the runtime's code. The runtime calls only `W` functions, so the half it does is a few conversions that were one variable away.
  - It costs a `.rsrc` section in GRIDE's link, a way to ask for it through three compilers' drivers and GRIDE's settings, and a test on each. That is a great deal of toolchain for section 1's first goal, which says nothing of resources.
  - Windows honours it from version 1903, and an older one ignores it without a word: the program would print the wrong text instead of failing. Section 1 promises any machine with Windows. The switch needs nothing newer than `CP_UTF8`.
  - It is fixed at link time for the whole program, and it cannot be asked for in the source.
- **Why the switch.**
  - It is a call in the program's source, in any of the languages: Shalimar's `c.setlocale` was already in the catalogue. No new name, so catalogue, headers and compilers stay as they are.
  - The same object links with both runtimes, and the UCRT has the very same switch. So section 1's rule still has a meaning here, and the probe's transcript is the UCRT's own.
  - Section 10.3's cost is one test of a variable per write. Everything else is in members only `setlocale` reaches: `win\utf8.c`, and `c\multibyte.cpp`, which took the multibyte functions from `c\wide.cpp` and `c\stdlib99.cpp`. `puts` alone grew 512 bytes and `printf` 1,536. Before that split `llabs` had pulled in all of `wide.cpp`, 47 KB.
- **What the program asks with.** `.UTF-8`, `.UTF8` or `.65001`, in any case, for `LC_CTYPE` or `LC_ALL`. `setlocale` answers `.UTF-8`, which given back selects the same. RTS6x's `setlocale` stays the C6747's; grtc has its own, `c\setlocale.cpp`. Only the encoding changes: there are no languages, so `de_DE.UTF-8` is refused, where the UCRT would also bring German's decimal comma, and the other categories stay `"C"` and refuse the name.
- **What asking changes.** Paths both ways (`__grt_widen`, `_getcwd`, `_findfirst`, the temporary names) and the time zone's name. `MB_CUR_MAX`, `mblen`, `mbtowc`, `wctomb`, `mbstowcs`, `wcstombs`, the restartable forms, `btowc` and `wctob`, because a locale that says UTF-8 and converts Latin-1 would corrupt text silently. `printf`'s `%ls` and `%lc`. `fputwc`, `fputws` and the `wprintf` family on a text stream.
- **The console: UTF-16, not `SetConsoleOutputCP`.** A console's code page belongs to the console, not the program. Setting it changes what the shell and every other program on that console see, and it stays changed after a crash, which no exit code can restore. `WriteConsoleW` and `ReadConsoleW` change nothing and work under any code page. Reading needs it anyway: `ReadFile` on a console gives `?` for a character outside the code page. `ReadConsoleW` does not end the input at CTRL+Z, so a line that begins with one is the end here, as `ReadFile` answers it. A console in binary mode still takes the bytes as they are.
- **Where it differs from Microsoft's, on purpose.** Each of these is a place where the UCRT gives a wrong answer and the rule "the same bytes" would keep it.
  - The console. The UCRT converts through the console's code page, so on code page 437 `tests\utf8\console.cpp` shows `Grüße ?????? ??? ??` and GRIDE's runtime shows the text.
  - argv and the environment. The UCRT leaves them in the ANSI code page, where `fopen(argv[1], "r")` after asking cannot find a name it just printed. Here `setlocale` respells them in the vectors main holds; a string taken earlier stays as it was, as a `getenv` pointer stays valid. Asking in a static object's constructor comes before argv is made, and needs no respelling.
- **What stays.** Reading wide characters from a text stream (`fgetwc`, the `wscanf` family, `%ls` in `scanf`) and `%S` in a wide format still take a byte for a character: the "wide characters" row, where they belong. So do `towupper` and `towlower`, which in the UCRT's UTF-8 locale also convert past ASCII. Of the narrow tables one answer stays "C"'s and is not the UCRT's: `isprint` of a tab, true there. `_setmode` still refuses `_O_U8TEXT` and its like.
- **The UCRT's oddities, followed.** `fputwc` of a surrogate writes nothing, sets EILSEQ and answers the character, not WEOF; the `wprintf` family writes nothing for one, where the "C" locale writes `?`. `%lc` of a surrogate is not compared: `snprintf`'s buffer then holds bytes the UCRT never wrote.
- **The tests**, `tests\utf8\utf8.ps1`, at -O2, -O0 and on the debug libraries:
  - `utf8.cpp`, through `probe.ps1`, 941 lines against the UCRT's transcript: a program that has not asked, then asked, then back in `"C"`. Names past the ANSI code page through `fopen`, `_stat64i32`, `_access`, `rename`, `remove`, `_mkdir`, `_chdir`, `_getcwd`, `_findfirst64i32` and `_rmdir`, each looked at by Windows' wide functions. Every conversion on valid, cut and illegal input.
  - `args.cpp`, in a folder and on a file past the ANSI code page: asked in `main` and in a constructor, argv opens the file and every string is the exact UTF-8; not asked, byte for byte the UCRT's.
  - `console.cpp` under `screen.c`, on a hidden console: the screen read back, and lines typed at it, one of them the name of a file it then makes. Not asked, the same screen as the UCRT's. The console's code pages are the same before and after. `console.expected` was written from the source, not from a run. Its U+FFFD is the character past one UTF-16 unit, which `ReadConsoleOutputCharacterW` answers for it whoever wrote it, the console's own echo too. `getchar`'s bytes show it arrived whole.
  - **A program that does not ask** (a check of this work, not a committed test): `args.cpp` and `console.cpp` linked with the libraries from before this work and with these give the same bytes and the same screen, with names and typed text past the ANSI code page.
  - **The rest**, on cpp11 1.61, the build checks clean: every probe at -O2 and -O0, on the release and the debug libraries, with `exitnow`'s known difference on the debug libraries on Microsoft's run alone; `filing.ps1` 198 of 198, `threads.ps1` 30 of 30, `debug.ps1`, the completeness test 301 of 301, and `tests\shalimar\access.ps1` 76 of 76 over both. cpp11's conformance suite passes 1,038 of 1,040 at each level. `cmath_c99_names` differs as before. `op11_recursion_tail` recurses 100,000 deep and overflows the default 1 MB stack, on main's tools without this work too, and passes with `/STACK:8388608`.
- **Found on the way, for link's owner:** a program linked with a second import library of KERNEL32.dll crashes: as it starts when that library comes before the runtime's, at the first call through it when it comes last. `tests\utf8\utf8.ps1` makes one import library for `screen.c` to avoid it.

**One member to a wrapper (2026-11-05).** Stage 4's `runtime\Wrappers.cpp` was one library member for 33 functions, so a program borrowing `labs` linked `atof`, `setlocale`, `remove`, `time` and the rest with it. It is 33 files now, one function each, in `runtime\wrappers\`.
- **Files, not one source compiled 33 times.** Each of the four builds already takes one source to a member. cl compiles a list in one process, `WindowsLibrary` takes a folder and the C6747's lines run a file at a time, while a single source would need a process and a define for each function, in cl's two lines, cpp11's two and a new mode of `WindowsLibrary`. shc.vcxproj's lists and grt.vcxproj's `SourceDirs` take the folder whole, so a wrapper the catalogue adds needs no project changed. A file includes only the headers its own function names.
- **A stale file still fails the build once.** ShalimarLibrary compares each file with what it would write, and owns the folder: one that differs or is missing is rewritten, one no row asks for is removed, and either fails the build. shc's runtimes take `Library.inc` as an input, since removing a wrapper leaves no newer file for them to see. Tried on copies, each failing once and passing the next time: a file edited by hand, a file deleted, a stray file, the catalogue dropping `labs`, and the catalogue giving the C6747 `system`.
- **`shm_c_system` is empty on the C6747**, whose target has not done `system`, as it was an `#ifndef` inside `Wrappers.cpp`. So `lib\tms6747\shmrt` holds a 0-byte `.s` and the library a member defining nothing. asm6x, lnk6x with every object named, vm6747 and sim6747 all take it.
- **The figures**, one program per wrapper against the build before, linked as `access.ps1` links them (196 links, and 6 of a program borrowing nothing, which are the same size as before). Every link took its own wrapper's member and no other, and `Borrow.obj` for all but the five wrappers that use nothing of it (`difftime`, `frexp`, `modf`, `remquo`, `scalbln`), which were paying for it before. Each of the six libraries defines the same names as before, and `Library.inc` did not change.

| Way | Saved per program, smallest to largest | Median | `labs`, before and after | Members linked for `labs` |
|---|---|---|---|---|
| grt, Release (file) | 26,624 to 35,840 bytes | 33,792 | 222,208 to 187,904 | 165 to 118 |
| grt, debug (file) | 43,008 to 58,880 | 56,320 | 440,832 to 384,000 | 165 to 120 |
| Microsoft's, Release (file) | 0 to 1,024 | 512 | 151,552 to 151,552 | 387 to 338 |
| Microsoft's, debug (file) | 512 to 2,048 | 512 | 428,544 to 428,032 | 403 to 358 |
| tms6747, Release (loaded sections) | 27,194 to 35,266 | 32,342 | 230,248 to 197,203 | 142 to 95 |
| tms6747, debug (loaded sections) | 50,452 to 66,843 | 62,398 | 442,514 to 379,520 | 149 to 105 |

- **Microsoft's line gains little**, and that is the link, not a miss: cl's `/O2` gives each function a COMDAT, so `/OPT:REF` already dropped the wrappers nobody called. The members left out now were read and thrown away. cpp11 wrote all 33 into the member's one code section, which is why grt and the C6747 carried the whole of it. Sizes on Windows move in steps of 512 bytes.
- **Checked** on cpp11 1.61, GRIDEd's Release build clean, with `tests\shalimar\access.ps1` 76 of 76 over the release runtimes and over the debug ones. Not run: the solution's Debug configuration, the other probes, the example sets, the toolchain demo, the Setup. macOS is not built here, and its Makefiles (above) need the new folder.

**`wmain`, `WinMain` and `wWinMain` (2026-11-05).** Section 7's four entries, the first item of the "later" row.
- **`grtstart`.** `start.c` keeps what the entries share, `__grt_start(wide)`, and each entry is a member of its own (`main.c`, `wmain.c`, `winmain.c`, `wwinmain.c`), because each names the program's function; a cl object's `_fltused` no longer pulls in `mainCRTStartup`. `mainguard.asm` became `entryguard.asm`: `__grt_call_entry` calls any of the four under the same language handler, so a fault reaches its signal's handler from any entry. The arguments now come before the initialisers, as in Microsoft's start-up, and `__argc`, `__argv` and `__wargv` are `<stdlib.h>`'s, spelled as the UCRT's over `__p___argc` and its like. `win\winargs.c` is a member of its own, so a program with `main` imports neither `GetCommandLineA` nor `GetStartupInfoW`. `win\utf8.c`'s `__grt_set_utf8` now respells `__argv` itself, and a wide entry has no narrow argv for it to respell.
- **link** had the rule wrong. Any `wmain` or `wWinMain` made its start-up the wide one, so `main` beside `wmain` started in `wmain`, and `WinMain` with no `/subsystem:` was a console program asking for `main`. It now does what link.exe does, measured on all 36 combinations of twelve sets of the four names and three `/subsystem:` choices (`tests\start\linked.expected`, recorded from link.exe by `start.ps1 -Record`). One difference is kept: a link that defines none of the four is a console one, where link.exe stops with LNK1561, since a library may yet bring `main`.
- **cpp11** gave the three names C++ names. `Parser::microsoftEntry` now keeps them as written at file scope for Microsoft's names, whatever their parameters and with nothing declaring them, as cl does: an overload is refused, as cl's C2731 refuses it, and a member, a function in a namespace or a variable of that name stays a C++ name, as do all three on the Itanium targets. One difference from cl remains: `static int wmain` stays internal, where cl ignores the `static`. **c90** needs nothing, and a function of either compiler that falls off its end already answers 0, as cl's `wmain` does. `windows.h` gains `HINSTANCE`, `HMODULE`, `WCHAR`, `LPWSTR` and their like, `APIENTRY`, the `SW_` constants and `GetCommandLineA` and `GetCommandLineW`, and declares `WinMain` and `wWinMain`.
- **A program without standard handles** behaves as section 7 says, which is every windows program started outside a console. Finding it showed something broader: the UCRT writes stderr's output from a temporary buffer when the call ends, so `fprintf` and `fputs` on a stderr that cannot be written still answer their count, with the error indicator set. grt answered -1. `grt::putCall` gives the UCRT's answer, and `fputc`, which has no such buffer there, keeps its EOF.
- **The catalogue** has 314 entries: `__argc`, `__argv` and `__wargv`, and the four entries, a new kind `entry`, which CatalogueCheck holds to their library and to no header. `completeness.ps1` cannot name an entry beside `main`, so it links one small program for each, with no `/entry:` and no `/subsystem:`, and each must run the program's function.
- **The tests.** `tests\start\start.ps1` runs six probes, each entry by c90 and by cpp11, at -O2, at -O0 and over the debug libraries, each byte-identical to the UCRT's transcript. Each program starts itself once per row through `CreateProcessW`, so that a child's command line, environment, show command and handles are exact: spaces, tabs, quotes and backslashes, an empty command line, non-ASCII text, 21 arguments, a return, `exit`, falling off the end, a fault reaching a handler, and no handles at all. `probe.ps1` no longer passes `/subsystem:console`, so link takes it from the object. The link table is 36 of 36. The completeness test links 304 of 304 names and starts 4 of 4 entries. Every earlier probe at -O2 and -O0, threads 30 of 30, `filing.ps1` 198 of 198 lines, `debug.ps1`, `math99.ps1` at both levels, `utf8.ps1` and `access.ps1` 76 of 76 over the release and the debug runtimes are unchanged, and the build's LibraryClosed, FrameCheck and CatalogueCheck pass.
- **cpp11's gate**, no version bump: all 1,040 valid cases pass at each level, all 888 invalid ones are refused, census 1,781 of 1,781, nothing regressed against 1.61's freeze. On GRIDE's runtime 1,039 pass at each level, and the one that differs from Microsoft's is `cmath_c99_names`, by its known 1 ulp.
- **Decided, and not done.** A failed `assert` in a windows program writes to stderr, in the spirit of decision 8, where Microsoft's runtime asks in a dialog and then exits 3, and with no handles nothing is seen before it ends; the dialog would need user32 and a test that presses a button. GRIDE's engine still passes `/subsystem:console` first, so a windows project sets `/subsystem:windows` in its link flags; there is no Options setting for it. `_environ`, `_wenviron`, `_wgetenv`, `_pgmptr`, `_wcmdln` and `DllMain` are not added. Not run on this work: both example sets, the debugging exercise and any entry but `main` under gride-dbg, the benchmarks, the Setup test, GRIDE 2.0 and 1.0, and GRIDEd's Debug configuration, which builds the same libraries.

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
