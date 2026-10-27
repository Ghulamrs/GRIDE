# cppparse

A C++98 tool that reads a C++ project and writes a complete report about it:
the files, every preprocessor directive, all classes with their inheritance,
methods, fields and nested types, the functions with their parameters and
local variables, globals, macros, exception handling, and a class diagram.

Written in strict C++98 with no dependencies beyond the standard library.

## Input

```
cppparse [options] <input>...
```

| input                   | what happens                                                      |
|-------------------------|-------------------------------------------------------------------|
| `Project.sln`           | every C++ project in the solution is read                         |
| `Project.vcxproj`       | Visual Studio 2010 and later: file list, include dirs, defines   |
| `Project.vcproj`        | Visual Studio 2005/2008                                           |
| a folder                | scanned recursively for `.h .hpp .hxx .hh .inl` and `.cpp .cxx .cc .c` |
| `a.h b.cpp ...`         | the files given                                                   |

Files reached through `#include "..."` that are not part of the input are
parsed too and marked as *discovered* (disable with `--no-follow`).

## Output

Written next to the first input, or into the `-o` directory:

| file                   | content                                                         |
|------------------------|-----------------------------------------------------------------|
| `<name>-report.txt`    | the complete text report (8 sections, see below)                 |
| `<name>-health.txt`    | version 2 health check: errors, warnings, info with codes        |
| `<name>-symbols.map`   | linker-style map: modules, symbols, cross reference, unreferenced |
| `<name>-symbols.json`  | symbol table for tools (one object per symbol)                   |
| `<name>-symbols.txt`   | symbol table, tab-separated                                      |
| `<name>-ast.txt`       | version 3: annotated AST of every function body                  |
| `<name>-ir.txt`        | version 3: class layouts, IR before/after `-On`                   |
| `<name>-opt.log`       | version 3: optimisation passes and instruction counts            |
| `<name>-asm.txt`       | version 3: lowered VM64 assembly                                  |
| `<name>.asm`           | version 4: x86-64 MASM (Windows x64 ABI); `--assemble` adds `.obj` via ml64 |
| `<name>.obj`           | version 5: COFF object written by cppparse itself (`--obj`) |
| `<name>.map`           | object map: sections, every symbol's address, real size and type (see below) |
| `<name>-report.html`   | the same report as one self-contained page with the diagram      |
| `<name>-report.pdf`    | the same report as PDF: title page, diagram pages, text pages    |
| `<name>-diagram.svg`   | the class diagram picture                                       |
| `<name>-diagram.dot`   | Graphviz source: `dot -Tpng -o diagram.png <name>-diagram.dot`   |

Report sections: 1 project, 2 files with every directive, 3 classes in
detail (methods with parameters, local variables and exceptions; fields;
typedefs; enums; friends; nested classes), 4 inheritance tree, 5 global
definitions (free functions, out-of-line member definitions, global
variables, static member definitions, typedefs, enums, macros, namespaces),
6 exception handling, 7 health findings, 8 totals.

Diagram legend: solid line with hollow triangle = inherits (red when
private, orange when protected, labelled when virtual); dashed blue = has a
member of that type; dotted grey = nested in; yellow box = abstract; grey
dashed box = base class not defined in the parsed files.

## Options

```
-o <dir>         output directory
-n <name>        base name of the output files
--no-follow      do not parse #include'd files outside the input
--no-directives  omit the per-file preprocessor listing
--no-locals      omit local variables
--text-only      write only the .txt, health and symbol files
--stdout         print the text report, write nothing
--health         print the health report, write nothing
--strict         exit code 3 when the health check finds errors (for build scripts)
-O0 .. -O3       version 3 optimisation level (default -O0)
--no-compile     skip version 3 and 4
--no-x86         do not write the x86-64 .asm file
--assemble       run ml64 on the .asm to produce the .obj (ml64 on PATH)
--obj            write the .obj with cppparse's own COFF writer (version 5)
--msvc-mangling  MSVC-compatible decorated names, so MSVC code links with plain C++ declarations
-E               version 6: preprocess only and print the result (like cl /EP)
--parse6         version 6: parse with the new front end (counts, syntax errors, <name>-ast6.txt)
--sema6          version 6: --parse6 plus semantic analysis (<name>-sym6.txt)
--compile6       version 6: --sema6 plus IR generation and the back end (<name>6.asm, <name>6.obj with --obj);
                 with --obj an input <name>.asm (a listing in cppparse's MASM subset) is assembled by the COFF
                 writer alone into <name>6.obj
-I <dir>         include directory for the preprocessor
-D NAME[=value]  predefine a macro; -U NAME undefines one
-j <n>           worker threads (default: one per processor for parsing, at most 4 for compiling;
                 never more than the number of files read)
src\*.cpp        wildcard patterns are expanded by cppparse itself (more than 50 files is fine)
@list.txt        response file: paths, patterns and options separated by white space (any number per line),
                 a path with spaces in double quotes, as cl's; a line starting with '#' is a comment
-q               quiet: print only diagnostics (no statistics, no list of the files written)
```

The version 6 outputs (`--parse6`, `--sema6`, `--compile6`) of an input are named after its stem (`two_a.cpp` gives
`two_a6.obj`, `two_a-ir6.txt`, ...); `-n` names them instead, numbered `name-0`, `name-1`, ... when there are several
inputs. An output that cannot be written (a missing `-o` directory) is reported and makes the exit code 1.

## Version 2: health checker

`<name>-health.txt` (also section 7 of the reports) lists what is wrong or
risky in the code, each finding with a code, severity, location, message
and a one-line explanation. Severities: **E** will not compile or link,
**W** likely bug or risky construct, **I** worth knowing.

Each finding is written in the Visual Studio / MSBuild error format, so the
Output window, the Error List and VS Code link it to the source line:

```
1>src/main.cpp(9): error E100: include file not found: #include "Missing.h"
1>    searched next to the including file and in the project include directories
```

`1>` is the number of the finding's project, counted from 1 in solution
order (a folder or a list of files is project 1), like the prefix MSBuild
gives each project's output; the explanation line carries the same prefix.
The HTML report shows the findings with the Error List's columns: severity,
code, description, project, file, line.

| area            | codes |
|-----------------|-------|
| preprocessor    | E100 include not found, E101 unbalanced `#if`, W102 no include guard, I103 circular include |
| syntax          | E110 missing `;` after class body, W111 unrecognised construct, I112 macro invocation, E113 unterminated body |
| class design    | E120 duplicate class, E121 duplicate/self base, W122 base not defined, E123 duplicate member, E124 incomplete type, W125 virtual methods without virtual destructor, W126 static member never defined, I127 derived class still abstract, W128 hides a base virtual |
| implementation  | E130 definition without declaration (reports const/parameter mismatch), E131 duplicate definition, W132/W133 declared but never defined, E134 return type mismatch, E135 default argument repeated, E136 initializer names unknown member, W137 initializer order, W138 member not initialised |
| calls and flow  | W140 missing return, W141 return value in void, E142 wrong number of arguments, E143 no such member, E144 `.` on pointer or `->` on object, E145 no matching constructor, I146 call to undeclared function, W147 throw in destructor |
| bodies          | W150 unused parameter, W151 unused local, W152 shadowing, W153 `if (x = y)`, W154 `if (...);`, W155 catch by value, E156 handler after `catch(...)`, W157 unreachable code |
| one definition  | E160 global defined twice, W161 extern never defined, E162 duplicate enumerator, E163 function defined twice, W164 global defined in a header, W165 non-inline function in a header |

The checks are static and declaration-level: macros are not expanded and
`#if` conditions are not evaluated, so code that depends on them can give
false positives. Argument checks compare counts, not types; full type
checking needs an expression parser and is planned for version 3 together
with an AST and a three-address IR. The folder `examples/sample-bad/` contains one
example of every finding and is part of the regression test.

## Symbol table and map file

`<name>-symbols.map` follows the layout of a linker map: a module table
(one line per file), the symbols of each module with a `module:ordinal`
reference, kind, name with signature, line range and the link to the
definition or declaration, then all symbols sorted by name, a cross
reference (every function and global with the places that call or use it),
the functions and globals never referenced anywhere, and the entry point.
`<name>-symbols.json` and `-symbols.txt` hold the same symbols for tools.

## Object map

Whenever x86-64 code is generated, `<name>.map` (`<name>6.map` with `--compile6`) describes the object in
the layout of a Visual Studio linker map (`link /MAP`), built from the COFF writer's own layout. Without `--obj`
the code is assembled in memory for it and no object is written.

```
 Start         Length     Name                   Class
 0001:00000000 00007031H  .text                  CODE
 0002:00000000 00000008H  .data                  DATA
 0004-00a9     000012c4H  .text$mn               CODE  COMDAT, 166 section(s)

 Publics by Value

  Address         Size        Flags  Type      Symbol                                    Name
 0001:00000120   000002dbH   f      function  ??0Matrix@mx@@QEAA@HHN@Z                  mx::Matrix::Matrix(int, int, double)
 00aa:00000000   00000010H          vtable    ??_7overflow_error@std@@6B@               std::overflow_error::`vftable'
```

Addresses are `section:offset` in the object (a linker adds the image base when it links). **Size** is the
number of bytes the encoder produced for the definition, without the alignment padding after it, which a
Visual Studio map does not show. Every function starts on a 16-byte boundary and data at its own alignment
(see *Alignment of functions and data* below). **Flags** are link's: `f` a function, `f i` an inline one, none for data
(see *Link's `f` / `i` flags* below). Types: function, data, vtable, string, constant. The symbol is the name in
the object (MSVC-decorated with `--msvc-mangling`), followed by its readable name. Publics and static
(internal-linkage) symbols are listed separately, as in Visual Studio's map; COMDAT sections (inline functions,
template instances, vtables) are grouped by name. The map starts with a time stamp in link's form
(`Timestamp is 6ac8c311 (Fri Oct  9 15:33:53 2026)`: seconds since 1970 in hex, then the local time; the
object itself keeps a zero time stamp, so it stays the same from run to run) and ends with **Symbols by
Name**: every symbol again, public and static, sorted by its readable name, with a Linkage column. Visual
Studio's map has only the address order. If the assembly cannot be encoded, the map says so and lists
the lines instead of showing guessed numbers.

### Alignment of functions and data

**Functions.** Microsoft's documentation never says that functions are 16-byte aligned: the PE format only
defines the section flags (`IMAGE_SCN_ALIGN_16BYTES`: "Align data on a 16-byte boundary. Valid only for object
files."), `/ALIGN` sets the alignment of whole image sections (default 4 KB), and `/Gy` puts each function in a
COMDAT section of its own without mentioning alignment. The alignment comes from the compiler: cl marks every
function's section (`.text$mn`) with an alignment flag, and the linker starts each section on that boundary,
filling the gap with `int 3` (0xCC). The flag depends on the optimisation option, as measured with cl 14.52
(`dumpbin /headers` of the object, addresses from `link /MAP`):

| cl option | `.text$mn` flag | function addresses |
|---|---|---|
| `/Od`, `/O2`, `/Ot` | 16 byte align | every function on a 16-byte boundary |
| `/O1`, `/Os` | 4 byte align | 4-byte boundaries (offsets 0, 4, 8, 12 seen) |

Exceptions under `/O2`: exception-handling funclets (`__catch$...`) share a section and are not aligned, and
much of the C runtime library is not built this way (in the matrix program 482 of 736 functions are 16-aligned
overall, 139 of 144 in the program's own code, the other 5 being `catch` funclets). No directive changes a
function's alignment: there is no `#pragma align` in MSVC (cl warns `C4068: unknown pragma 'align'` and ignores
it), and `__declspec(align(N))` or `alignas(N)` on a function is rejected (`C2024: 'alignas' attribute applies to
variables, data members and tag types only`).

**Data** is placed at its natural alignment, as the x64 ABI recommends (`char` 1, `short` 2, `int` and `float` 4,
`double`, pointers and `__int64` 8, `__m128` 16; a structure takes its largest member's), never padded to 16 by
default. `__declspec(align(N))` and `alignas(N)` raise a variable's or a type's alignment. `#pragma pack` and
`/Zp` change only the packing of members inside a structure, union or class, not where a variable or a
function is placed. Measured in one object: `char` globals at consecutive bytes, a `double` on an 8-byte
boundary, `__declspec(align(32)) int` on a 32-byte and `alignas(64) int` on a 64-byte boundary; in the matrix
program `double` constants (`__real@...`) on 8-byte and 16-byte SSE constants (`__xmm@...`) on 16-byte
boundaries.

**cppparse** follows cl's speed and debug layout: every function in `.text` starts on a 16-byte boundary with
`int 3` before it, and COMDAT functions have 16-byte aligned sections of their own (cppparse has no size mode,
so there is no 4-byte packing). Data is placed at its type's or declared alignment (`__declspec(align(N))`,
`#pragma pack` for members), at least the natural boundary of its size up to 8 bytes, as cl does. The map's
sizes stay the encoded bytes; the padding shows as the gap before the next function.

### Link's `f` / `i` flags

`link /MAP` marks symbols in its *Publics by Value* and *Static symbols* lists, and cppparse's map does the
same:

- **`f`**: a function: its symbol has the function type (`0x20`, which the PE format defines as "function");
- **`f i`**: an *inline* function: one that may legitimately appear in many objects, of which the linker
  keeps one (inline member functions, template instances, implicitly generated members such as copy
  constructors);
- **no flag**: data (variables, constants, vtables, strings).

Microsoft does not document these markers: the `/MAP` page lists the map's contents but never explains `f`
or `i`. Their meaning was established from cl's own objects. With `/O2` cl puts every function in a COMDAT
section of its own (`/Gy`), so `i` is not "in a COMDAT section"; it follows the COMDAT's selection, which
`dumpbin /symbols` shows. In cl's `matrix.obj` of the matrix sample all 31 `f i` functions have selection
`IMAGE_COMDAT_SELECT_ANY` (duplicates allowed, any one kept) and all 22 plain `f` functions have
`IMAGE_COMDAT_SELECT_NODUPLICATES`:

```
f i  ??0Matrix@mx@@QEAA@AEBV01@@Z       Matrix(const Matrix&): implicit copy constructor, "pick any"
f    ??0Matrix@mx@@QEAA@HHN@Z           Matrix(int, int, double): defined in matrix.cpp, "no duplicates"
f    ?identity@Matrix@mx@@SA?AV12@H@Z   Matrix::identity(int): defined in matrix.cpp, "no duplicates"
```

cppparse uses COMDAT sections only for inline functions and template instances, always with "pick any",
and puts every other function in the plain `.text`. Its rule (a function in `.text` is `f`, one in a COMDAT
section of its own is `f i`) therefore gives link's answer for every object cppparse writes.

### What the suite checks in a map

Tests 8 and 13 check every map they produce (`Test-ObjectMap` in `sandbox/build-cppparse.ps1`):

- **addresses**: every symbol's `section:offset` is the one `dumpbin /symbols` reports for the object;
- **sizes**: no definition runs into the next one; every function starts on a 16-byte boundary with less
  than 16 bytes of padding before the next, and the last one ends its section;
- **flags**: a function in the object's own `.text` is `f`, a function in a COMDAT section of its own
  (absent from the section table, listed only in its group) is `f i`, and data has no flags;
- **Symbols by Name**: the list repeats exactly the symbols of the address lists, with the same address,
  size and flags, in ascending order of readable name.

Test 13 also checks, on the matrix sample, that vtables are typed `vtable` and that COMDAT sections are
grouped, and on `examples/sample-align` that `g_align64` sits on a 64-byte boundary.

### Unwind tables

Every function with a frame gets the Windows x64 unwind information that cl gives its functions: an
`UNWIND_INFO` in `.xdata` (`$unwind$f`) and a function table entry in `.pdata` (`$pdata$f`: begin, end and unwind
info as image-relative addresses). With them, Windows can step back from any point in the function to its
caller and restore the caller's callee-saved registers. Debuggers, profilers and crash dumps use this, and so
do exceptions. A frameless leaf function needs none: Windows assumes the return address at `[rsp]` there.

The code generator describes its prologue with the MASM directives (`PROC FRAME`, `.pushreg`, `.allocstack`,
`.savexmm128`, `.endprolog`), and the COFF writer turns them into the tables as ml64 does. A COMDAT function's
tables go into associative COMDAT sections, so the linker keeps or drops them with the function. `rsp` does not
move in a function body, so the tables describe the frame from `rsp` alone. `rbp` is listed as a pushed register,
not as a frame register.

**Table-based exceptions (`--eh=table`, the default since this version).** A try region, or a cleanup region for destructors, costs no code
on the normal path: no frame to push, no `setjmp`, no pop at the exits. The function's unwind info names the
runtime's `cppparse_eh_handler`, followed by cppparse's range table: `CPPX`, a count, then for each range its
begin, end and landing pad. The landing pad is the region's dispatch stub; the code generator marks the ranges
with `;@ehrange` lines, which are comments to ml64. `throw` walks the stack with the unwind tables
(`RtlLookupFunctionEntry`, `RtlVirtualUnwind`). It enters the first landing pad whose range holds a frame's call
site with `RtlRestoreContext`, so that frame gets its callee-saved registers back as they were at the call; the
register allocator therefore treats every call or throw inside a region as a possible jump to the region's
handlers (and those of the regions around it), so what the code after a catch reads stays in its register
across them (X86.cpp computeEhEdges). The
same walk also finds the setjmp frames of version 5's model (`--eh=sjlj`): it longjmps to one once it has passed
the stack frame that holds it. Both models can therefore be mixed in one program. A handler body is a region of
its own: an exception that leaves it has the runtime release the exception the handler held
(`cppparse_catch_unwind`), unless it is that same exception (`throw;`). The design and its steps are in
[docs/design-table-exceptions.md](docs/design-table-exceptions.md).

Checks: test 8 compares every table with the one ml64 builds from the same assembly text, byte for byte.
`eh.ps1` goes further. An ml64 function sets every callee-saved register (rbx rbp rsi rdi r12-r15, xmm6-xmm15) to
a pattern and calls cppparse code that recurses with values of its own in those registers, through a frame of
more than a page. At the bottom, the runtime's `cppparse_walk_frames` walks back with `RtlVirtualUnwind`. It
must find every cppparse frame, and the registers it restores in the ml64 frame must be the patterns.
`eh.ps1` then runs the exception cases of `probe.cpp`:
- throw and catch by class, base class, `int` and `double`;
- destructors in the frames in between, nested regions, rethrow, a throw from a handler;
- a frame of more than a page, locals kept across a throw, a loop that throws on every other iteration;
- a throw across a second file (`probe-mix.cpp`);
- destructors that throw (C++98 allows it): through `delete` of a base pointer, of a stack object, of a member,
  of an element in `delete[]` and of an array at scope exit, and a constructor that throws in `new T` and `new T[n]`
  (ehDtorThrow, ehArrays);
- `catch (T x)` by value: the handler's copy is copy-constructed and destroyed (ehCatchValue);
- a thrown pointer caught as a pointer to a base (adjusted, also a null one and a rethrown one), with more
  cv-qualifiers, as `void*`, and not as a private or ambiguous base (ehCatchPointer);
- a constructor that throws in a new-expression frees through the replaced global operators (ehGlobalFree).

These run in four models (setjmp, table, and each mixed with the other in `probe-mix.cpp`) at `-O0` to `-O3`.
Each output must equal the cl build's. That reference is built with `/EHa`, because cl 19.52 (preview)
miscompiles a throw out of a recursion with destructors under `/EHsc` at `/O1` and above, and with
`/Zc:implicitNoexcept-`, C++98's destructors that may throw. `terminate.cpp`, a whole program, checks that an
exception leaving a destructor run by the stack unwinding calls terminate (the output and the exit code of cl's
build, table and setjmp models at `-O0` and `-O2`); so does `terminate-catch.cpp` for a copy constructor that
throws while it initializes a by-value catch parameter.

### Debug information

Every object the version 6 pipeline writes carries CodeView line tables in `.debug$S` sections, as cl's objects
do: one section for the functions of `.text` (with the module's file checksum and string tables), and an
associative COMDAT section per COMDAT function, each with an `S_GPROC32` (`S_LPROC32` for internal linkage) and
`S_END` symbol and a `DEBUG_S_LINES` table that maps every code offset where the source line changes to that line
(the function's own line at offset 0, the prologue). The IR carries the source line of every instruction; the
listing shows them as `;@debug "<file>" <name>` after a `PROC` and `;@line N` before the code of a new line
(comments to ml64), which the COFF writer turns into the tables. Code inlined at `-O1` and up belongs to the
call's line. `link /DEBUG` puts the tables in the PDB, so Visual Studio, WinDbg and dbghelp map an address of
cppparse's code to its file and line; without `/DEBUG` the linker drops the sections, and the map lists them as
`DEBUG`. Test 18 builds `examples\sample-dbg\lines.cpp`, links it with `/DEBUG` and has
`examples\sample-dbg-driver\dbgcheck.cpp` (cl, dbghelp: `SymGetLineFromAddr64`, `SymGetLineNext64`) read the
lines of three functions back. Not provided: types, locals and parameters (`.debug$T`, `S_LOCAL`), inlinee line
records, column numbers.

## Building and testing

```
powershell -ExecutionPolicy Bypass -File cpp\sandbox\build-cppparse.ps1 [-Config Release|Debug] [-NoTests]
```

Compiles the `.cpp` files of `driver`, `common`, `frontend`, `middle`, `backend`, `analyzer` and `analyzer/report` with MSVC at
`/W4 /permissive-` (warnings fail the build). `-Config Release` (default)
builds with `/O2` into `sandbox/cppparse.exe`; `-Config Debug` builds with
`/Od /Zi /RTC1 /D_DEBUG /MTd` and links with `/DEBUG` into
`sandbox/debug/cppparse.exe` next to its `.pdb`, with objects kept apart in
`obj/debug`. `-NoTests` stops after the build. The test groups (17 as of
version 6) run against whichever configuration was built. Test 17 holds the
front-end regressions of `examples/sample-front/`: units that must be
rejected with their diagnostics and no object, and programs whose output
must equal that of cl's build (`front.cpp`; `copies.cpp`, which logs every
construction, copy and destruction of its classes and temporaries, against
cl `/Od` at `-O0` and cl `/O2` at `-O2`).

A Visual Studio 2022 solution does the same build inside the IDE:
`cpp\cppparse.sln` with the project `cppparse\cppparse.vcxproj` (x64,
Debug and Release, `/W4 /permissive-` with warnings as errors, output in
`sandbox\vs\<Configuration>\`). The project compiles only the folders of
`cppparse\`; `include\`, `platform\`, `libcpp\`, `runtime\`, `docs\` and this
README appear in Solution Explorer as non-compiled items, each under a filter
of its folder's name (`cppparse.vcxproj.filters`). Every list is a wildcard,
so a new file is picked up without editing the project. The project names the v143 toolset; a
newer Visual Studio retargets it on opening, and MSBuild builds it with
`/p:PlatformToolset=<installed toolset>`:

```
msbuild cpp\cppparse.sln /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145
```

The scripts in `sandbox\scripts\` run the version 6 pipeline outside the
test suite: `lib.ps1` and `life.ps1` (the library and lifetime samples
with their native drivers), `selfhost.ps1` (compiles cppparse's own
sources with cppparse and links `cppparse-self.exe`), `mix.ps1` (builds a
layout-compatible reference with cl against cppparse's headers and swaps
cppparse's objects in one unit at a time), `cprog.ps1` (builds the multi-file
C sample `examples/sample-c/` with cl as C and as C++ and with each generation
of cppparse, and compares the programs' output), `mapcompare.ps1` (builds the matrix sample with `cl /O2`
and with `--compile6 -O2 --obj`, links both with `/MAP`, checks cppparse's object maps against the linker's map
of the same objects (addresses, 16-byte alignment, `f` / `f i` flags) and every function's map size against
the code bytes `dumpbin /disasm` finds in the object, and lists the functions both compilers emit side by side
with their exact code sizes (cl's counted from its objects the same way, since `link /MAP` has no size column;
cl's functions are the ones `dumpbin /symbols` finds defined in its objects, so an inline method `link /OPT:REF`
dropped, such as `Matrix::rows()`, is listed as removed by link instead of missing), then a per-class summary
(every member function of `mx::Matrix` and `lin::System` either compiler emits, both sizes, the ratio, whether
each linker kept it, class totals); `-Lvl` picks cppparse's optimisation level, exit code 1 on any disagreement
of the object-map checks), `eh.ps1` (exceptions and
unwind tables, `examples/sample-eh/` with its driver and an ml64 entry function, at `-O0`..`-O3`; see
[Unwind tables](#unwind-tables)). The first four: the Visual Studio 2010 solution in
`examples/sample/` with checks on the report, symbols and map, the `cpp` folder
itself as a folder input (cppparse parses and health-checks its own
sources), `Vector.hpp` + `VectorTest.cpp` as plain files, and the broken
folder `examples/sample-bad/`, which must produce every health code. Any C++98 compiler works for the sources themselves, e.g.
`g++ -std=c++98 -pedantic -Wall -Icppparse/driver -Icppparse/common -Icppparse/frontend -Icppparse/middle -Icppparse/backend -Icppparse/analyzer -Icppparse/analyzer/report -o cppparse cppparse/{driver,common,frontend,middle,backend,analyzer,analyzer/report}/*.cpp` (on a host without Windows the worker pool runs the jobs one after another).

## How it works

`Lexer` turns a file into tokens, keeping each preprocessor line as one
token. `Parser` walks the tokens once: it records every directive and keeps
the stack of active `#if` conditions, skips `#if 0` regions, recognises
include guards, and parses namespaces, classes (templates, specializations,
multiple and virtual inheritance, nested classes), functions (declarations,
definitions, constructor initializer lists, function-try-blocks), variables,
typedefs, enums, friends and usings. Function bodies are skipped by brace
matching while counting `try`/`catch`/`throw` and recording local variable
declarations. `CodeBase` loads the project, follows includes and links
out-of-line definitions to their declarations, base names to classes, and
static member definitions to fields. `Diagram`, `Report`, `Html` and `Pdf`
write the outputs.

It is a declaration-level parser, not a compiler: it does not expand macros
or evaluate `#if` conditions (it reports them instead), and constructs it
cannot classify are listed as diagnostics and skipped.

## Limits

* Macros that hide syntax (`BEGIN_MESSAGE_MAP(...)`, `DECLARE_...()`) are
  recognised heuristically as macro invocations and listed as diagnostics.
* `struct X y;` versus `class MACRO Name;` is decided by a heuristic
  (an all-caps first word is treated as an attribute macro).
* Local variable detection is syntactic; `a * b;` at statement start is
  taken as a declaration of `b`, as a compiler would.
* Property sheets (`.props`) referenced by a `.vcxproj` are not evaluated.

## Version 3: AST, type checking, IR, optimisation and lowering

Every function body (templates excepted) goes through a small compiler
back half, all in C++98:

```
tokens -> AST (BodyParser) -> type check (TypeCheck) -> IR (Ir) -> passes (Optimizer) -> VM64 assembly (Lower)
```

Outputs: `<name>-ast.txt` (annotated syntax tree per function),
`<name>-ir.txt` (class layouts with field offsets and vtables, then the IR
before and after optimisation), `<name>-opt.log` (what each pass changed
per function, with instruction counts) and `<name>-asm.txt` (the lowered
assembly). Select the level with `-O0` (default), `-O1`, `-O2` or `-O3`;
`--no-compile` skips version 3 entirely.

**Type checker** findings (T codes, in the health report): T100
undeclared identifier, T101 no such member, T102 no viable overload by
argument types, T103 ambiguous call, T104 bad conversion, T105 invalid
operands, T106 non-const method on const object, T107 assignment to const,
T108 return type mismatch, T109 condition not convertible to bool, T110
member access on non-class, T111 bad subscript, T112 bad dereference, T114
unknown type, T115 signed/unsigned comparison, T116 narrowing, T117 `.`
on pointer or `->` on object, T118 unused expression result, T119 private
member access, T121 lvalue required, T122 break outside loop, T123 to
T125 switch problems, T146 call to an undeclared (library) function, T190
and T191 constructs outside the subset. For bodies the checker handles,
the typed checks replace the count-based E142 to I146 of version 2.

**IR**: temporaries assigned once, locals as memory slots with
`loadlocal`/`storelocal`, `addrfield` with real offsets from the class
layout, `vcall` through vtable slots, `br`/`condbr`/`ret`, `trybegin`,
`throw`, `catchparam`. Objects are handled by address; `__copy`, `__new`
and `__delete` are runtime calls.

**Passes**: `-O1` constant propagation and folding, store-to-load
forwarding, copy propagation, algebraic simplification and strength
reduction (`x * 8` to `x << 3`), local common-subexpression elimination,
dead code and dead store elimination, branch folding, unreachable block
removal, block merging. `-O2` adds loop-invariant code motion (dominators,
natural loops, preheaders) and inlining of small functions. `-O3` inlines
larger functions and fully unrolls loops with a constant trip count of at
most 8. The folder `examples/sample-opt/` shows each transformation; the test
checks, for example, that `constants()` folds to `ret #14` at `-O1`,
`square` is inlined at `-O2` and `unroll()` becomes `ret #6` at `-O3`.

**Lowering** targets a virtual 64-bit machine with eight integer and
eight floating registers: linear-scan register allocation with spilling,
frame layout for locals and spill slots, arguments in R0-R3/F0-F3 then on
the stack, virtual calls through the vptr, `TRY`/`ENDTRY` pseudo
instructions for exception regions.

Subset limits: function-try-blocks, brace initialisers, goto and local
classes are reported and skipped; standard-library types are opaque and
never produce findings; calls through unknown functions are emitted as
opaque calls (the `<cmath>` functions that take and return `double`, such
as `fabs`, `sqrt` and `pow`, are called with `double` arguments and
results). A function that uses a standard-library class by value
(`std::vector<double>`, `std::string`, a stream), or a class whose layout
contains one, is not compiled: its size and field offsets would be
guesses. It is reported as T190 and counted as skipped; `--compile6`
compiles such code with cppparse's own headers.

## Version 4: SSA, template instantiation, x86-64

**SSA** (`Ssa.cpp`): after the version 3 passes every scalar local that
never has its address taken is promoted to SSA temporaries with phi
instructions placed by iterated dominance frontiers; global value
numbering runs over the dominator tree, trivial phis are folded, and the
phis are removed again with parallel copies on split critical edges
before lowering. `-ir.txt` shows the SSA form of every function.

**Template instantiation** (`Instantiate.cpp`): `FixedStack<double, 32>`
or `Buffer<int, 4>` used in a declaration becomes a real class with the
parameters substituted in fields, signatures, bases, typedefs and every
method body (in-class or out-of-line); `maxOf(7, 2)` deduces `T` from the
argument types and instantiates the function template. Instantiated
bodies are parsed, type checked and compiled in extra rounds; integral
arguments such as `R - 1` are folded; dependent uses inside other
templates are left alone; a user-written explicit specialisation wins.
Member templates and partial specialisations are not instantiated.

**x86-64** (`X86.cpp`): `<name>.asm` holds MASM for the Windows x64 ABI:
callee-saved registers allocated by linear scan, frame with shadow space,
arguments in RCX/RDX/R8/R9 and XMM0-XMM3, SSE2 floating point, vtables in
`.data`, strings and float constants in `.const`, the global `operator
new`/`operator delete` (by MSVC's names, since version 6) and `memcpy`
for the runtime helpers, `cppparse_throw` for `throw` (exceptions are not
implemented on this target). `--assemble` runs `ml64 /c` to produce
`<name>.obj`. Symbols use cppparse's own mangling (`square(int x)` is
`square_int_x`); `extern "C"` functions keep their name.

Test 6 assembles the `sample-opt` output with `ml64`, links it with the
MSVC-compiled `examples/sample-opt-driver/driver.cpp` and runs it at `-O0`, `-O2`
and `-O3`: all 19 result checks pass, templates included.

## Version 5: parallel pipeline, many input files

**Worker pool** (`Parallel.cpp`): C++98 has no threads, so the pool uses
`CreateThread` on Windows (a sequential fallback elsewhere). A job is a
`ParallelJob` with `run(index)`; `runParallel(job, count, threads)` hands
the indices to the threads through an interlocked counter.

**Parallel loading** (`CodeBase::loadFiles`): every input list (project,
solution, folder, or the files on the command line) is read, lexed and
parsed on the pool, one file per job, writing only into its own
`SourceFile`. Files are registered in list order, then the `#include`s
they discovered form the next round, until nothing new appears. On the
49 files of cppparse itself parsing takes 32 ms on 12 threads instead
of 125 ms.

**Parallel compiling** (`Compiler.cpp`): the per-function phases run on
the pool: body parse + type check + IR build (and the template
instantiation rounds), the local optimisation passes, the SSA passes, and
VM64 plus x86-64 lowering. Inlining and unrolling read the IR of other
functions, so that phase runs on one thread. The shared structures (type
table, template instantiator, class layouts, the x86 constant pools) are
guarded by one recursive lock; results are merged in function order and
x86 constant labels are derived from their contents (`fconst_<bits>`,
`str_<hash>`), so every output file is byte-identical whatever `-j` is.
The optimisation log ends with the time of each phase.

The compile phases allocate heavily and all threads share the C runtime
heap: on a 12-processor machine they are fastest at 2 to 4 threads and
get slower beyond, which is why the default compile thread count stops
at 4 (`-j` overrides it). Arena allocation for the IR and the generated
text is on the version 6 list.

**Many files**: `src\*.cpp` and `include\*.hpp` are expanded by cppparse
(no shell globbing is needed on Windows), and `@list.txt` reads paths,
patterns and options from a file, one per line, so a command line is
never too long. All plain files are parsed together as one project.

**COFF object writer** (`Coff.cpp`, `--obj`): `<name>.obj` is written by
cppparse itself, so ml64 is no longer needed to get an object file. The
writer is a small assembler for exactly the MASM subset that `X86.cpp`
emits: it parses the text, encodes every PROC on the worker pool (REX,
ModRM, SIB, RIP-relative operands, the SSE2 scalar instructions, and
jump relaxation to the 2-byte form where the target is near), then lays
out `.text`, `.data` and `.rdata`, the relocations (`REL32` for calls,
`lea` and RIP-relative loads, `ADDR64` for vtable entries), the symbol
table (functions, globals, vtables, string and float constants, externs)
and the string table. Lines it cannot encode are reported with their
line number and the file is not written. Test 8 links the object with
the MSVC-compiled driver at `-O0` and `-O3`, runs it (all 19 checks
pass) and compares `dumpbin /disasm` of cppparse's object with ml64's:
the instruction streams are identical.

**Exceptions** (`runtime/cppparse_rt.cpp`, generated by `X86.cpp`):
`throw`, `try`/`catch` by value or reference, `catch (...)`, rethrow with
`throw;`, matching through base classes, and throwing through functions
without handlers all work on the x86-64 target. The mechanism is
setjmp/longjmp: every try region owns a 272-byte frame in the function's
stack frame; entering the region registers it with the runtime and calls
`_setjmp(frame, NULL)`; leaving it (normally, or by `return`, `break` or
`continue`, for which the IR builder emits the pops) unregisters it.
`throw x` copies the object into runtime memory together with a type
descriptor (`"BadIndex;Failure"` lists the class and all its bases, or
the scalar type name) and longjmps to the innermost frame, whose dispatch
stub asks the runtime to match each catch clause in order and otherwise
rethrows to the outer frame. A handler fetches the object address with
`cppparse_catch_begin` and frees it with `cppparse_catch_end`. The try
body always starts a new block, so no value defined inside the region
dominates the handlers: after a longjmp the callee-saved registers hold
the values they had when the region was entered, and locals stay in
memory (functions with try regions are not put into SSA form). Limits:
no destructors run during unwinding, thrown objects are copied bytewise
(no copy constructor), and function-
try-blocks are still skipped. The runtime is linked with the driver in
tests 6 and 8, which check all of the above and that no frame or handler
is left registered.

**MSVC-compatible names** (`Mangle.cpp`, `--msvc-mangling`): the x86-64
output (and so the `.obj`) uses the decorated names cl.exe gives the
same declarations: `?square@@YAHH@Z`, `?sum@Point@@QEBAHXZ`,
`??0Point@@QEAA@HH@Z`, `?add@?$Buffer@H$03@@QEAAXAEBH@Z`,
`??$maxOf@H@@YAHAEBH0@Z`, `?g_shapeVersion@@3HA`, `??_7Circle@geom@@6B@`.
Covered: namespaces and nested classes, access and static/virtual kinds,
const methods, constructors, destructors and operators, the fundamental
types, pointers and references with cv-qualifiers, class/struct/union
and enum types, template instances with type and integer arguments, and
the name and parameter-type back-references. `extern "C"` functions and
`main` keep their names. Test 9 compiles the sample with cl, checks that
every function symbol cl defines is spelled identically in cppparse's
object, then links `examples/sample-opt-driver/driver-native.cpp`, which declares
the functions as plain C++, against cppparse's object and runs it.

## Source layout

`cppparse/` holds only the compiler's own sources. The version 6 sources are grouped by the stage of the
compiler they belong to; every folder is an include directory of the build (`build-cppparse.ps1`,
`cppparse.vcxproj`), so `#include "X.hpp"` works across them.

- `driver/`: `main` (command line and the modes).
- `common/`: `PathUtil` (strings, paths, files), `Parallel` (the worker pool), `Target` (the target
  description, see below), `Literal` (the values of character and string literals as cl gives them, shared by
  the preprocessor, sema, IrGen and the back end).
- `frontend/`: `Pp` (preprocessor), `CxxAst`/`CxxParser` (syntax tree and parser), `Sema`/`SemaExpr`
  (semantic analysis).
- `middle/`: `IrGen` (IR from the analysed tree), `Ir` (the intermediate representation), `Optimizer`, `Ssa`.
- `backend/`: `BackEnd` (the shared driver of the phases below), `Lower` (VM64), `X86` (x86-64 assembly),
  `Coff` (the object writer), `Mangle`/`MangleSema` (MSVC name decoration, part of the target's ABI).
- `analyzer/`: the version 1 to 5 pipeline: project reading and the old compile path kept until the version 6
  pipeline replaces it: `Lexer`, `Parser`, `Model`, `CodeBase`, `ProjectReader`, `Ast`, `BodyParser`, `TypeCheck`,
  `Types`, `Instantiate`, `IrBuilder` (old IR generation and class layouts) and `Compiler` (the old driver).
  They are exercised by tests 1 to 9.
- `analyzer/report/`: the health checker and the reports, off the compiler's path: `Health`, `Report`,
  `Symbols`, `Diagram`, `Html`, `Pdf`. Only `driver/main` and `CodeBase::checkHealth` call into them.

Everything that is not compiler source sits next to `cppparse/` in the `cpp` folder:

- `include/`: cppparse's C++98 library headers (`<string>`, `<vector>`, `<map>`, `<iostream>`...) and the C
  headers C++98 defines (`stdio.h`, `stdlib.h`, `string.h`, `ctype.h`, `math.h`, `stddef.h`). Only standard
  headers live here.
- `platform/`: the minimal Windows declarations cppparse's own sources use (`windows.h` for the worker pool,
  `io.h`, `direct.h`, `sys/stat.h`, `sys/types.h` for the file helpers). They are not a Windows SDK: only the
  self-build passes `-I platform` (`sandbox/scripts/toolset.ps1`), and each stops with `#error` when
  `_WIN32` is not defined.
- `libcpp/` (`cout`, `cerr`, `cin`), `runtime/` (the exception runtime, built by cl for the Windows target),
  `examples/` (the test inputs), `docs/`.

## Targets, predefined macros and platform code

A target says what cppparse compiles for: its predefined macros, the sizes of `long`, `wchar_t`, `long double`
and pointers, the type of `size_t`, the calling convention, the name mangling and the object format
(`common/Target.cpp`). `--target` selects it:

| target | macros | sizes | ABI, mangling, objects | status |
|---|---|---|---|---|
| `x86_64-windows` (default) | `_WIN32=1`, `_WIN64=1`, `_M_X64=100`, `_MT=1`, `_CPPUNWIND=1` | `long` 4, `wchar_t` 2, `long double` 8, `size_t` = `unsigned long long` | Microsoft x64, MSVC, COFF | complete |
| `x86_64-linux` | `__linux__`, `__unix__`, `__x86_64__`, `__LP64__`, `__ELF__` (and the short spellings) | `long` 8, `wchar_t` 4, `long double` 16 (aligned 16), `size_t` = `unsigned long` | System V, Itanium, ELF | `-E`, `--parse6`, `--sema6`; no back end yet |

Every target also has `__cplusplus=199711L`, `__cppparse__=6` and `__STDC__=1`. `_MT` and `_CPPUNWIND` say what cl says
for the programs cppparse builds (the static multithreaded CRT, as `/MT`, and C++ exceptions, as `/EHsc`; test 17
checks both against cl). `_MSC_VER` and `_DEBUG` are never predefined: code under them is MSVC's alone. The Linux sizes and macros agree with
`clang --target=x86_64-linux-gnu`, which will be the reference for the Linux back end as cl is for Windows.
`--compile6` refuses a target without a back end, and stops at a missing include file instead of compiling
against undeclared names (the analysis modes only warn). A unit with syntax or semantic errors is not compiled
either, as with cl: the errors are listed, the exit code is 1, and no object, `.asm` or map is written (one left
by an earlier run is removed; test 17 checks this on `examples/sample-front/errors.cpp`).

The host is a different matter from the target: cppparse's own sources are strict C++98, and every call outside
the standard library sits behind `#ifdef _WIN32` with a standard fallback. `common/Parallel.cpp` uses Windows
threads there and runs the jobs one after another elsewhere (C++98 has no threads); `common/PathUtil.cpp` uses
the MSVC directory functions there and POSIX `opendir`/`getcwd` elsewhere; the debug-build assertion hook of
`driver/main.cpp` is under `_MSC_VER`. When cppparse compiles itself for the Windows target it defines `_WIN32`
and takes the Windows branches, with the declarations of `platform/`.

Library names the checkers recognise in analysed code are grouped by origin (`libraryNameOrigin` in
`common/PathUtil.cpp`): ISO C90 functions and macros, C99 additions, the C++98 library, later C++, POSIX, the MSVC
runtime, Win32 functions and Win32 macros. `isStandardLibraryName` is true for ISO C90 and C++98 only; this is
data about the analysed program, so none of it depends on the host.

## Version 6: a clean front end (in progress)

Versions 1 to 5 grew a compiler out of a declaration reader. Version 6
rebuilds the front end the way a compiler is built, keeping the back
end (IR, optimiser, SSA, x86-64, COFF, runtime) and the execution tests
as the acceptance bar:

1. **Preprocessor** (`Pp.cpp`, done): translation phases 1 to 4.
2. **Grammar parser** (`CxxAst.*`, `CxxParser.*`, done): one parser for
   declarations and statements, producing a single AST for whole
   translation units.
3. **Semantic analysis** (`Sema.*`, `SemaExpr.*`, first version done):
   real types, scopes, name lookup, overload resolution and template
   instantiation on the AST.
4. **IR generation** (`IrGen.*`, `--compile6`, done): from the analysed
   tree into the shared back end, with object lifetimes (destructors,
   copy constructors, temporaries, unwinding; done).
5. **Library headers** (`include/`, `libcpp/`, done): cppparse's own
   C++98 `std` and C headers, with COMDAT objects and dynamic
   initialisation so that several translation units link.
6. **Reports** rebuilt on the new AST, and the version 4 and 5 gaps.

Every stage works per translation unit and runs on the worker pool.
The long-term target is a complete C++98 compiler that compiles
itself, so every stage is measured against cppparse's own sources.

Version 6 is finished only when every compilation gap left by versions
4 and 5 is closed:

- templates: member templates and partial specialisations, and
  instantiation on the AST instead of by token substitution;
- function-try-blocks and friend functions defined inside a class,
  which the old front end skips;
- real declarations for the standard library (cppparse's own C++98
  headers: `vector`, `string`, `map`, `set`, `cstdio`, ...), which also
  removes the untyped library-call results of the old front end;
- exceptions: destructors run during unwinding and thrown objects
  copied with their copy constructors (both done in the version 6 IR
  generator), a per-thread frame stack (done in the runtime);
- COFF: `.pdata`/`.xdata` unwind tables and line information;
- mangling: function-pointer parameter lists, member pointers (done), varargs;
- the type checker replaced by real semantic analysis (scopes, lookup,
  overload resolution, conversions, templates).

**Parser** (`CxxParser.cpp`, `--parse6`): a recursive-descent parser for
the C++98 grammar over the preprocessor's tokens, producing the
`cxx::TranslationUnit` tree of `CxxAst.hpp`: declarations with
decl-specifiers and declarators (pointers, references, arrays,
functions, member pointers, nested declarators, bit-fields, exception
specifications), classes with bases and access, enums, namespaces,
using declarations and directives, templates (type, non-type and
template template parameters, specialisations, instantiations),
linkage specifications, all statements, and all expressions including
casts, `new`/`delete`, `sizeof`, `typeid` and `throw`. The parser keeps
a scope stack of the type names it has seen (typedefs, classes, enums,
template parameters, injected class names, names brought in by using
directives and anonymous namespaces) and the members of every named
class or namespace, so `T * x` is a declaration only when `T` is a type,
`f < x` starts template arguments only when the innermost scope that
declares `f` declares it as a template (a local variable, a parameter or a
function named `count` hides `std::count` of `<algorithm>`),
constructors are recognised by their name, out-of-line members see
their class's nested types, and reopened namespaces remember their
earlier parts. Unknown qualified names such as `std::vector<int>` are
accepted as types where the syntax demands one, so sources parse without
the standard headers; the C library typedefs (`size_t`, `FILE`, ...) are
assumed. Ambiguous statements, casts, `sizeof` and `new` placements use
tentative parsing. After a syntax error the parser skips to the next
`;` or `}` and continues. `--parse6` prints token and node counts with
the syntax errors and writes `<name>-ast6.txt`. Test 11 parses
`sample-opt`, the VS2010 sample and all 30 of cppparse's own sources
with zero syntax errors, and checks error recovery on `sample-bad`.

**Preprocessor** (`Pp.cpp`, `-E`, `-I`, `-D`, `-U`): line splices,
comments, pp-tokens, `#include` (quoted form searched next to the
including file, then `-I` directories; `#pragma once`; a macro as the
file name), `#define` of object-like and function-like macros with `#`
and `##`, argument prescan, rescanning and hide sets (a macro never
expands inside its own expansion, as in `#define f(x) x + f`),
`#undef`, `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif` with full
constant expressions (`defined`, arithmetic, comparisons, `?:`,
character constants, octal and hex), `#error`, `#warning`, `#line`,
`#pragma pack` as MSVC has it (`pack(n)` with n = 1, 2, 4, 8 or 16,
`pack()`, `push`/`pop` with an optional identifier and value, `show`;
every token carries the packing in force, which applies at the next
class key), and `__FILE__`, `__LINE__`, `__DATE__`, `__TIME__`, `__cplusplus`
(`199711L`). The result is a token stream with file and line positions,
the list of included and missing files, all macro definitions and the
diagnostics. `cppparse -E -I inc pp.cpp` prints the preprocessed text
like `cl /EP`; test 10 compares the two on `examples/sample-pp/` (identical apart
from whitespace) and checks that `#error` and a missing include are
diagnosed.

**Semantic analysis** (`Sema.cpp`, `SemaExpr.cpp`, `--sema6`): types
are interned objects (fundamental, pointer, reference, array, function,
class, enum, member pointer, dependent) with cv-qualifiers; symbols are
the named entities (namespaces, classes, enums, typedefs, variables,
functions, enumerators, template parameters, class and function
templates) in scopes with overload sets and using-directives. The
analysis declares a translation unit in order, analyses member function
bodies once their class is complete, and for every expression records
its type, value category, constant value and the entity it refers to.
Name lookup is unqualified through enclosing scopes and base classes,
qualified through namespaces and classes, and for members with
inheritance; an entity found along two paths (`::printf` directly and
through `std`'s `using ::printf` after `using namespace std`) is one
candidate. Calls, constructors, conversions and operators on class
operands go through overload resolution with the C++98 implicit
conversion sequences (exact, promotion, standard, user-defined through
converting constructors and conversion operators, reference binding,
derived-to-base); an argument matching `...` gets the default argument
promotions (a float is passed as a double, bool, char, short and enums as
int), and `T()`, `new T()` and a mem-initializer `m()` value-initialise as
cl does (zeroed first unless a user-declared constructor builds the
object; front.cpp `varargs::`, `valinit::`). Classes get their layout (bases, vptr, fields,
alignment) and vtable (slots, overrides, abstractness) and implicit
special members. The layout follows MSVC's rules for `#pragma pack(n)`
(a member's alignment is capped at n) and `__declspec(align(N))` on a
class, a member or a variable (a floor that packing cannot lower,
inherited by classes that contain such a member); `alignas` is C++11 and
not accepted. Globals are placed at their alignment (at least the
natural boundary of their size, up to 8 bytes, as cl does), and a data
section is marked with the largest alignment it holds (S129 warns that
`__declspec(align)` on a local variable is ignored). Empty bases follow
MSVC without `__declspec(empty_bases)`: an empty base takes no space
(`struct EB : E { int w; }` is 4 bytes, w at 0), a byte of padding
separates a base ending with a zero-sized subobject from a following base
that leads with one (`EM : E, E2 { int w; }` is 8, w at 4), and an empty
base left at the end of a class sits on its last byte. Bit-fields are laid
out as cl does: a bit-field takes a unit of its declared type's size, the
next one shares it (from the low bit up) when its type has the same size
and its bits fit, and a zero-width bit-field closes the unit when it
follows a bit-field; loads mask and shift the unit (sign-extending a
signed field) and stores merge into it. The members of an anonymous
union or struct in a class are members of the class, placed at the
unnamed member that holds them. The polymorphic bases are placed first: the
first one is at offset 0 and the class shares its vptr and extends its
vtable (`DP : NP, PB` with a virtual PB is 24 bytes, PB at 0, NP at 16).
A class's new virtual functions take their slots in declaration order,
except that the overloads of a name sit together where the name is first
declared in the class (an overrider counts) and the last declared comes
first, as cl orders them (`a, f(int), b, f(double), ~V` gives `a,
f(double), f(int), b, ~V`; layout.hpp OV, OW); a non-virtual declaration
of the name counts too. An override is found among the slots of the
bases' tables, so a function a middle class hides is still overridden.
Every other polymorphic base subobject keeps its own vfptr, and the class
gets a secondary vftable for each (`Symbol::vftables`, named as cl names
them: `??_7SD@@6BSB@@@`), which the constructor stores next to the primary
one. As with MSVC, a virtual function first held by a secondary table
expects `this` at that vfptr (its this adjustor): callers add it, the
function moves `this` back on entry, and an override held by the primary
table as well is reached from the secondary one through an adjustor thunk
(`sub rcx, 16; jmp`, named `?f@SD@@WBA@EBAHXZ`, COMDAT); a destructor always
takes the complete object, so its secondary slots are thunks.
A virtual destructor's slot holds MSVC's vector deleting destructor
(`??_ESD@@UEAAPEAXI@Z`, COMDAT, the name cl's tables use): with bit 1 of
its flags clear it destroys the object and frees it when bit 0 is set;
with bit 1 set it destroys the array whose count is in the 8-byte cookie
in front of it, last element first, and frees it from the cookie when
bit 0 is set. `delete` calls it with flags 1 and `delete[]` with flags 3
(an empty array is freed from its cookie directly), as cl does, so
`delete` through any base frees the complete object, and objects and
arrays allocated by either compiler are deleted by the other (cl's own
tables name `??_E` as a weak external that falls back to its `??_G`;
cppparse emits only `??_E`). It frees through the class's own `operator
delete` (`operator delete[]`, given count * size + 8) when lookup in the
complete class finds one (with the complete object's size for the
two-parameter form), as a non-virtual `delete` does. A new-expression
calls the class's own (or inherited) `operator new` / `operator new[]`
with cl's size (count * size + 8 when the array has a cookie), and
`delete[]` frees through the class's `operator delete[]` (count * size + 8
with a cookie; without one the count is unknown and the two-parameter form
gets one element's size, as from cl); `::new` and `::delete` use the
global ones. The global ones are called by cl's names: `??2@YAPEAX_K@Z`
and `??_U@YAPEAX_K@Z`, the sized `??3@YAXPEAX_K@Z` for every `delete`
(also in the deleting destructors), the sized `??_V@YAXPEAX_K@Z` for
`delete[]` with a cookie and where a constructor throws, and
`??_V@YAXPEAX@Z` without a cookie. The C++ library defines them (its sized
forms call the unsized ones), so a replacement defined in any unit, by
either compiler, is the one called. The library's `operator new` reports
failure by throwing `std::bad_alloc` through cl's exception handling,
which does not pass through cppparse's frames: a failed allocation ends
the program. A
destructor stores its own class's vptrs on entry, as a constructor does
after its bases, so a virtual call made while a base part is built or
destroyed reaches that base's function. Virtual base classes are laid out
as cl lays them out (Sema::layoutClass): the class shares the vbptr of its
first non-virtual base that has one, or gets its own after the non-virtual
bases, the members moved down past it as cl injects it (`VN : NB, virtual
VB { short n; }`: vbptr at 8, n at 18); every virtual base, direct or
inherited, is placed once after the non-virtual part (nvSize), in cl's
order, and each vbptr has its vbtable (entries and the bases that name it
apart, `??_8VX@@7BVL@@@`); test 17 compares sizes and vbtables with cl's
`/d1reportAllClassLayout` (`examples/sample-front/vbase-layout.cpp`).
`--compile6` compiles the non-polymorphic ones as cl does: the vbtables
are read-only COMDAT data (`??_8D@@7B@`); a conversion to a virtual base
(and member access and calls through it) loads the vbptr and the base's
entry, a null pointer staying null; a constructor takes cl's "most
derived" flag as its last parameter (after `this` when variadic): the
creator passes 1, and the constructor then stores the vbptrs and builds the
virtual bases first, while the non-virtual bases get 0; `~D` (`??1`)
destroys only the non-virtual part and the vbase destructor (`??_D`),
which stack objects, members, arrays and `delete` use, destroys the
virtual bases after it. A virtual base with virtual bases of its own keeps
its vbptr in the virtual part, stored by the most derived constructor, its
table named after the base it comes through as cl names it (test 13,
sample-layout: a diamond, shared and injected vbptrs, nested virtual bases,
copies, arrays and logs, built and destroyed across both compilers).
Polymorphic classes with virtual bases are laid out and generated as cl
does (Sema::layoutClass, completeClass; IrGen storeVptr, registerMetadata;
X86 addVtordispThunk): the vfptr stays inside the virtual base subobject
and a class gets a vfptr of its own only for new virtual functions; each
class holds a copy of every virtual base's vftable at the base's place
(`??_7VL@@6B@`, `??_7VN@@6BV@@@`), whose overriders from outside the base
are reached through adjustor thunks computed from the base's offset; a
class with a user-declared constructor or destructor that overrides a
function of the virtual base gets cl's vtordisp (4 bytes before the base,
inherited by the classes derived from it) and those slots hold cl's `$4`
thunks (`?f@VL@@$4PPPPPPPM@A@EBAHXZ`: `movsxd rax, [rcx-4]; sub rcx, rax;
sub rcx, delta`); a constructor stores the base's vftable through the
vbtable and the vtordisp as the base's distance from its place in the
complete object, a destructor restores them, and the deleting destructors
`??_G`/`??_E` reached from a virtual base's table subtract the base's
static offset; a name declared in a virtual base and again in a class
deriving from it is found in the derived class (dominance). Empty and
over-aligned virtual bases, `#pragma pack` and vbtables named by a path of
several bases (`??_8VPR@@7BVPB@@VPP@@@`) follow cl as well. Test 17
compares the sizes, vtordisps and vbtables of 50 classes with cl's
`/d1reportAllClassLayout` (`vbase-layout.cpp`) and runs front.cpp
`pvbase::` (calls through every base during construction and destruction,
delete, copies, arrays, `.*`/`->*`) against cl at -O0 and -O2; test 13
(sample-layout PV..PVN2) builds, calls and deletes such objects across the
compilers, cl's derived class PVE with cl's vtordisp thunks included;
eh.ps1 ehCatchVbase catches a thrown class with virtual bases as a virtual
base, by value and by reference, and a thrown pointer to one as a pointer
to a virtual base, found through the pointed-to object's vbtable (the
runtime descriptor `EV*+v0:4`). Still refused with S130 (test 17,
`examples/sample-front/vbase.cpp`): a virtual base that overrides a
virtual function of another virtual base of the class (`WL : virtual VL`
with `VL : virtual V`: cl's vtordispex thunk reads the vbtable); `--sema6`
and the reports accept it. S132 (found while generating code, no object
written; `examples/sample-front/vbase-refused.cpp`) refuses a cast from a
virtual base down to a derived class (ill-formed, cl's
C2635). Pointers to members are generated in cl's representations
(IrGen.cpp mpLayout and the mp* functions): a data member pointer is an
int offset with null -1, under virtual inheritance an int offset and the
vbtable offset of the base's entry (null {0, -1}; cl reads `object + vbptr
offset + vbtable[entry] + offset`, entry 0 leading back to the vbptr's
subobject), and in the unspecified model (the class incomplete when the
size was first needed) the offset, the vbptr's offset and the entry (null
{0, 0, -1}); a member function pointer is a code pointer (cl's vcall thunk
`??_9P@@$BA@AA` for a virtual function, `mov rax, [rcx]; jmp [rax+slot]`),
under multiple inheritance with an int this adjustment (`&AB::fb`, first
held by a secondary vftable, is AB's thunk with the adjustment 16), under
virtual inheritance with the vbtable offset as well, and in the
unspecified model with the vbptr's offset before it. Their sizes are cl's
(TypeTable::sizeOf by the inheritance model: data 4, 8 with virtual
bases; functions 8, 16 with multiple or virtual inheritance or a vfptr
added in front of a base; an incomplete class 12 and 24, also after it is
defined when its size was needed while it was incomplete, as cl keeps the
model it chose there; front.cpp `mpsize::`, `review::`, `mplater::`).
Conversions to a derived class add the base's offset (not to a null data
member pointer; through a virtual base the entry and the offset in the
base), `static_cast` back subtracts it or drops the adjustment, `.*` and
`->*` apply them, `(obj.*pmf)(x)` calls through the adjusted this, `==`
compares every field (a function's beyond the code pointer only when it is
not null), and they travel as cl passes them: 4 and 8 bytes in a register
(also the {offset, entry} pair), larger ones by the address of a copy and
through a hidden result pointer that a member function takes before
`this`. `&C::m` and null initialise static storage as data; a
zero-initialised object gets cl's null values where cl writes them (a
pointer to member, an array of them, the pointer-to-member members of a
class without bases; nested classes, arrays of them inside a class and
arrays of classes stay zero), value-initialisation zeroes as cl does, and
a brace list's omitted pointers to members are null. The mangling is cl's
(`PEQP@@H`, `P8P@@EAAHXZ`, `?pm@@3PEQP@@HEQ1@`). Test 17 compares every
word of the representations with cl's (front.cpp `memptr::`) and test 13
passes them between the compilers (sample-layout MP, MQ, MH, VL). Still
refused with S132 (`memptr-refused.cpp`): a member function's pointer converted
down to a base of multiple inheritance, and a conversion between two classes of
the unspecified model through a virtual base; the other conversions are cl's
(row B11 of `docs/next-targets.md`), and a pointer to member is a template
argument as cl spells it (row B10). A direct base that is also a
base of another direct base is diagnosed as cl does (Sema::declareClass;
`ambiguous-base.cpp`): S141, an error, where cl gives C2584
(`struct F : virtual V, W {}` with `W : V`), S142, a warning, where cl
gives C4584 (`struct G : V, W {}`); ambiguous conversions to a base (cl C2594: S143 for
casts, S105 for implicit conversions) and unqualified access to a member of
a base held more than once (cl C2385: S144) are errors (Sema::baseSubobjects,
`ambiguous.cpp`), as is a virtual function of a virtual base overridden in
two direct bases and not in the class itself (cl C2250: S145,
Sema::completeClass); `d.B::a` and `d.C::f()` go through the named base
(IrGen namingBase: they used the first path; front.cpp `ambig::`).
An override with a covariant return type whose
conversion moves the pointer (`CD* self()` over `SB* self()` with SB at 16
in CD) is laid out as cl lays it out: the overrider gets a slot of its own
at the end of the vftable that holds the overridden function (in the
primary table among the class's new virtual functions, in declaration
order), and every slot it overrides holds cl's return-adjusting thunk,
named as the function, non-virtual, returning the overridden type
(`?self@CD@@QEAAPEAUSB@@XZ`): a frame with the stack arguments staged
again, the call, and a non-null pointer (a reference always) moved by
the base's offset; a further overrider gets thunks in every slot, also
where the move is 0, and a new slot again; an override whose moves are
all 0 stays in place. front.cpp `covariant::` and sample-layout RB/RC
check it against cl, through every base and across the compilers. S131
still refuses a slot inside a virtual base whose overrider is held by another
virtual base, and a variadic covariant function (`covariant-refused.cpp`); a
return conversion through a virtual base, a slot that also moves this and a slot
of a virtual base's vftable get cl's thunks (row B11).
Test 13 links
`examples/sample-align/` with a cl-compiled driver that compares every
size and offset with cl's and checks the globals' values and alignment;
`examples/sample-layout/` does the same for MSVC's class layout rules at
-O0 to -O3, with objects written by one compiler and read by the other. Class templates are instantiated from the syntax tree
with the parameters bound in an instantiation scope (defaults, partial
specialisations, out-of-line members); inside an instantiation the
template's name alone is the injected class name, and with arguments
(`Matrix<T, C, R>` inside `Matrix<T, R, C>`) it names that specialisation,
also as a qualifier. Function templates are
instantiated by deduction from the argument types; a function template
declared and later defined is one template (the definition takes the
declaration's default arguments). A member template of a class template
is declared with each instantiation of the class, so it sees the class's
bound parameters: deduction against `Matrix<T, R, K>` matches `T` and `R`
with their values and deduces only `K`. With these, `MatrixTest.cpp`
(`Matrix.hpp`, `Vector.hpp`) compiles and prints what cl's build prints
at `-O0` and `-O2` (test 17). Diagnostics carry
S codes (S100 unknown type, S101 undeclared identifier, S102 no such
member, S103 undeclared function, S104 const assignment, S105
conversion, S106 ambiguity, S107 no matching function, S113 incomplete
type, ...). `sizeof` follows 5.3.3: `sizeof(T&)` is `sizeof(T)`, and a
class that is only declared, a class inside its own member declarations,
`void` and function types are rejected with S113 as cl rejects them
(C2027, C2070) instead of measuring 8 bytes. The
`--sema6` mode prints the counts and writes `<name>-sym6.txt` with the
symbols, layouts, vtable slots and instantiations. Test 12 checks
`sample-opt` (no errors, every name and call resolved, `Buffer<int,4>`
and `maxOf<int>` instantiated with the right layouts) and the VS2010
sample. On cppparse's own sources every remaining diagnostic names a
type or function from a header that is not available (`std::`, the C
library, the Windows API): the next stage supplies those headers.

**IR generation** (`IrGen.cpp`, `--compile6`): walks every function
with a body, template instantiations and the implicit special members
that are used, and produces the same IR the old builder did, but with
every fact taken from the sema annotations: resolved symbols, field
offsets (with base-class adjustment), vtable slots for virtual calls,
chosen constructors and conversions, class layouts, throw descriptors.
Functions returning a class by value take a hidden result pointer;
reference parameters and results are addresses.
The IR still carries the old front end's `Type` objects through an
adapter, so the optimiser, SSA, lowering, x86-64 and COFF stages run
unchanged. `--compile6` runs the whole version 6 pipeline and writes
`<name>-ir6.txt`, `-opt6.log`, `-asm6.txt`, `<name>6.asm` and, with
`--obj`, `<name>6.obj`; `--msvc-mangling` uses a second mangler
(`MangleSema.cpp`) driven by the sema symbols, so template instances
and nested scopes get their decorated names from structure rather than
text. Test 13 compiles `sample-opt` through the version 6 pipeline at
`-O0` and `-O2`, links the object with the native-declaration driver
and the exception runtime, and runs it: all 19 checks pass, including
templates and exceptions.

**Object lifetimes** (`IrGen.cpp`, `examples/sample-life/`, test 14): every
constructed local, by-value parameter and class temporary is recorded
with a "constructed" flag byte. Block scopes destroy their objects in
reverse order on normal exit; `break`, `continue` and `return` unwind
the scopes they leave; temporaries die at the end of their full
expression (a temporary bound to a reference lives as long as the
reference; one that a `?:` result refers to is destroyed under its
flag). Copies go through the class's own members: the copy constructor
for initialisation, by-value arguments (the callee owns and destroys
its copy, as MSVC does, destroying its by-value parameters left to
right as cl does; the arguments are evaluated in cl's order, those that call
right to left, then those that only read: row A5 of `docs/next-targets.md`) and
return values (constructor calls and calls returning the class build
the result in place; at `-O1` and up, as cl `/O2`, a local of the
result's class declared directly in the function body and named by every
`return` is built in the caller's result object (NRVO, through the
hidden result pointer, not for results returned in `rax`) and destroyed
only when an exception leaves the function; `-O0` copies it as cl `/Od`
does), the copy assignment
operator for `a = b`; implicit copy constructors, assignments and
destructors are generated memberwise (bases first). Arrays of objects
are constructed and destroyed element by element; `new T[n]` keeps the
element count in front of the elements when `T` has a destructor so
`delete[]` can run them. A function that owns objects with destructors
gets a catch-all try region around its body whose handler destroys the
objects whose flag is still set and rethrows, so destructors run while
an exception unwinds through the function; a user `try` destroys the
objects built inside it at a handler entry; a constructor whose body
throws destroys the members and bases it already built. Destructors may
throw, as C++98 allows and cl (with `/Zc:implicitNoexcept-`) does: a
destructor's members and bases are cleanups of its body, so they are
destroyed when it throws, and the exception goes on to the handler (a
flag is cleared before its destructor runs, so a throwing destructor is
not run again); an element destructor that throws in `delete[]` or at
the end of an array's scope leaves the elements before it destroyed,
newest first; a `delete` whose destructor throws does not free, as cl.
An exception leaving a destructor that the unwinding itself runs calls
terminate (each landing pad's destructors are a region whose handler is
`cppparse_terminate`). A new-expression whose constructor (an element's,
for `new T[n]`, after destroying the elements built) throws frees the
memory through the matching operator delete, the class's or the global
one; a placement new frees nothing. `catch (T x)` copy-constructs the
handler's object from the thrown one and destroys it when the handler
ends (a copy constructor that throws there calls terminate); an
exception leaving the handler destroys its objects before the handled
exception is released. A handler for a base class that is not the first
gets that base subobject (the throw descriptor gives each base's
offset); as with cl, only a public base the class holds once is caught.
A thrown pointer is caught (C++98 15.3/3) by a handler for a pointer to
such a base, adjusted to the base subobject unless null, by one with more
cv-qualifiers on the pointee, and by `void*` for any object pointer; a
`throw` of a string literal is caught by `char*`, as with cl. A goto out of a handler is refused (S132). `throw obj`
copy-constructs the exception object into heap storage that the
runtime owns (`cppparse_throw_owned`) and destroys with the class's
destructor when the handler ends. The driver `examples/sample-life-driver/
driver-life.cpp` counts constructions, destructions, copies and live
buffers across 24 checks at `-O0`, `-O2` and `-O3`. Functions with
such objects contain a try region, so the SSA phase skips them; the
block-local passes still run.

**Standard library** (`include/`, `libcpp/iostream.cpp`, `examples/
sample-lib/`, test 15): cppparse's own C++98 headers, written so that
the same files serve semantic analysis of cppparse's sources and real
compilation. `cstddef`, `cstdlib`, `cstdio`, `cstring`, `cctype`,
`cmath`, `ctime` and `cstdarg` declare the C runtime (`extern "C"`, with
`using ::name` in `std`; `size_t` is the 64-bit type of this target);
`exception`, `new`, `stdexcept`, `utility` and `algorithm` (pointer
ranges: `sort`, `stable_sort`, `reverse`, `find`, `lower_bound`, ...)
are implemented in place; `string` and `wstring` are `basic_string<charT,
traits, Allocator>` (with `char_traits<char>` and `<wchar_t>`, named as cl
names them), zero-terminated, up to 15 chars in the object and longer ones on the heap (MSVC's layout); `vector` keeps its elements in malloc storage built with
placement new and destroyed explicitly; `map` and `set` share the AVL
tree of `__tree` (parent links, erase unlinks nodes so other iterators
stay valid); the streams of `ios`, `streambuf`, `ostream`, `istream`,
`sstream`, `fstream` and `iostream` are the templates of clause 27
(`basic_ostream<charT, traits>` and the others, with the char and
wchar_t typedefs) over a `basic_streambuf`, with a locale (`<__locale>`:
ctype, numpunct, collate; `<__num_facets>`: num_put and num_get, through
which the streams write and read numbers); they format and parse without the C library's
variadic functions, and `std::cout`, `cerr`, `cin`, `wcout` and the
others are objects defined in `libcpp/iostream.cpp`, which cppparse
compiles and the program links; `complex`, `valarray`, `strstream` and
`locale` complete the C++98 headers (what they leave out is in
`docs/next-targets.md`, row C3).
To make that work across translation units, every inline function,
template instantiation and implicit member is emitted in a COMDAT
section of its own (selection "any") by the COFF writer, vtables
likewise, static const members with a constant initialiser are folded
rather than emitted, and globals that need construction are built by a
per-module initialiser registered in `.CRT$XCU`, so the C runtime runs
it before `main` (and registers their destructors with `atexit`). The
library exposed a long list of compiler defects
that are now fixed: per-instantiation copies of template syntax trees,
using-declarations, parameter names that are also type names, base
class type names in derived bodies, qualified lookup from nested classes,
pseudo-destructor calls, placement new, default arguments, conversion
operators and `operator->` as wrapper nodes, reference-returning calls,
`this` adjustment for members of a base, operator templates in the
mangler, string literal concatenation, indirect calls, and live
intervals of values that cross a loop's back edge. Test 15 compiles the
three samples and `libcpp/iostream.cpp` to four objects, links them
with `examples/sample-lib-driver/driver-lib.cpp` and runs 13 checks at
`-O0` and `-O2`. Measured against the self-hosting target: analysing
cppparse's own 39 sources with these headers (`--sema6 -I include`)
reports 78 errors in total, against 1548 without them; the rest are
overloads and conversions the analysis does not resolve yet, plus a few
types the headers still lack.

**Stage 6, first steps (self-hosting)**: with the headers, all 36 own
sources now analyse with zero semantic errors and every one of them
compiles to a COFF object; the 36 objects link with the exception runtime
and `libcpp` into `cppparse-self.exe`, a cppparse built by cppparse (it
works now, see the next paragraph). The steps that got here: forward declarations (`struct X;`) declare in
their own scope instead of resolving to an outer `X`; a function hides
a class of the same name in calls and in what the parser took for a
functional cast (`stat(path, &st)`); a call with too few arguments no
longer instantiates a function template; `vector`, `set` and
`set::insert` have range member templates with an integer-dispatch
trait (`__traits`); `windows.h`, `io.h`, `direct.h`, `sys/stat.h` and
`sys/types.h` declare the platform API the sources use (the interlocked
operations come from the runtime, they are compiler intrinsics in MSVC);
`string::insert(iterator, char)` and `rdbuf()`; a value-initialised
`double` member emits a float constant; static locals are module globals
initialised on the first pass under a guard byte (COMDAT when the
function is inline, named per function so every unit agrees); `goto`
and labels are blocks (a `goto` destroys the objects of the scopes it
leaves, innermost first, and leaves try blocks, as `break` does; one that
jumps past an object with a destructor or into a try block is refused,
S132);
anonymous-namespace symbols carry a per-unit id (`?A0x<hash>@`) so units
do not collide. The version 6 mangler now spells every name cl.exe gives
cppparse's own sources (checked unit by unit with `sandbox/scripts/mix.ps1`,
which links each cppparse object among cl-built ones): class and enum
results carry `?A` (`?B` when const), template instance names start fresh
back-reference tables with the template's own name as entry 0, a function
template name takes no slot, parameter back-references are keyed by type
(so `AEBVstring@std@@` and its later spelling `AEBV12@` share one slot, a
top-level `const` makes a separate one, two-character codes like `_N`
count), `const` template arguments are escaped (`$$CB`), member template
instances keep their access and constructor templates are `??$?0`, and
globals follow cl's variable encoding (`3PEBDEB`, `3PAPEAUItem@1@A`,
`3PAY02HA`). Test 15 compiles `examples/sample-mangle/mangle.cpp` with
both compilers and requires every decorated name cl uses to be spelled
identically. The old front end skips the extensionless standard
headers when it resolves `<...>` includes, since it assumes the `std::`
names as before.

**Stage 6: the self-built compiler works** (`examples/sample-self/`, test 16):
`sandbox/scripts/selfhost.ps1` builds `cppparse-self.exe` from the 36 objects
cppparse makes of its own sources at `-O0`, runs it on `sample-opt` and requires
its object and listings to be byte-identical to those of the MSVC-built
compiler. The self-built compiler also compiles `sample-life`, the three
`sample-lib` units, `libcpp/iostream.cpp`, `sample-self` and all 36 of its own
sources at `-O0` and `-O2` to the same bytes as the reference build (cl with
cppparse's headers). That reference is used rather than the MSVC-built
`sandbox/cppparse.exe` because the latter links MSVC's library, whose
`std::sort` may order equal elements differently from cppparse's and so
could change a register assignment at `-O2`; on `sample-opt` the two give
identical output, so no such difference has been seen yet. The defects
found on the way, each reproduced in
`sample-self` and checked against cl's result by `driver-self.cpp`:
aggregate initialisation (nested and elided braces, arrays of unknown bound
sized from their initializer, zero fill of what is not initialised;
`const char* const k[] = {...}` used to get 8 bytes); a `?:` temporary bound
to a reference lost the flag that says whether its arm ran, the operand of
another type was not converted (`c ? "xmm" + s : kNames[r]` as a string), an
lvalue operand next to a prvalue was not copied, and two class lvalues that
differ in const are an lvalue; default arguments of a declaration were lost
when the out-of-line definition replaced the parameters; indices and pointer
differences were 32-bit (`long` is 32-bit here) and `sizeof` was
`unsigned int`; integer literals above `LONG_MAX` were clamped (types now as
cl: `0xFFFFFFFF` is `unsigned int`, `2166136261` is `unsigned long`; values
up to 64 bits are read whole and typed as cl, `long long` and `unsigned long
long` included; one outside the 32-bit sign-extended range is built at run
time, not a constant expression; only beyond 64 bits S127 warns), `char` is signed and escapes in character and
string literals (octal, `\x`) are decoded; constant folding of unsigned
operations and of casts to narrow types; narrow integer results are
re-extended after every operation, call and same-size signedness change (the
upper bits of a register used to keep carries that division and indexing
read); `a op= b` computes in the common type; constructor templates were not
constructors (no member initialisers ran); `&v[0]` through `operator[]`
loaded the element instead of taking its address; `it++` on a class called
the prefix operator; null pointers stayed null only without a base offset,
and `static_cast` to a derived class did not subtract it; slicing copies and
assignments of polymorphic classes keep their own vptr; a static reference
local had the size of its referent; `float` constants and statics were
doubles; frames of a page or more call `__chkstk`; float constants are
emitted as their exact bits; floating -> integer conversions follow cl's
code (signed and narrow targets through a 32-bit `cvttsd2si`, so out of
range they give 80000000h truncated, `unsigned int` through a 64-bit one,
`unsigned long long` by cl's two-range sequence; `-O2` folded
`(unsigned)3e9` to 2147483648), `unsigned long long` -> floating converts a
value with its top bit set by halves as cl does, the optimiser folds both
the same way (Optimizer.cpp floatToInt), and value numbering compares
floating constants by their bits (it took `1e19` and `1e19f` for one value;
sample-front `conv::`; floating constants converted to integers are folded
modulo 2^N as cl folds them, and float literals are rounded once from their
decimal, front.cpp `cfold`); loop unrolling (`-O3`) renamed the values a
loop uses from before it, such as hoisted invariants. The old type checker
recursed without end on a copy constructor when it looked for a converting
constructor (`c ? T(1) : t`). For linking with MSVC-compiled code,
constructors return `this` and functions with a hidden result pointer return
it in `rax` as cl does, narrow results of calls are extended, the `new[]`
cookie is 8 bytes and the exception runtime's frame chain is per thread.
Floating point follows cl: `long double` is the 8-byte double (converting
between the two is no instruction); `float op float` is a float computed in
single precision (`addss`, constants folded with float rounding), as is
`-f`; comparisons use `ucomiss` for floats and `ucomisd` for doubles and test
the parity flag, so NaN is unequal to everything and true as a condition,
and `-0.0` is false; the optimiser keeps only the identities exact under
IEEE (`x * 1`, `x / 1`, `x - 0.0`, `x + -0.0`) and tells constants apart
by their exact value (test 17, `fp::` in `sample-front/front.cpp`).
Class arguments and results follow cl's x64 rules (IrGen::regParamType,
regReturnType): a class of 1, 2, 4 or 8 bytes without a non-trivial copy
constructor is passed as an integer of that size in its register or stack
slot (floats inside it too), any other by the address of a caller-made copy
that the callee destroys; a C++03 aggregate of those sizes (no user-declared
constructor, destructor or copy assignment, no bases, no virtual functions,
no private or protected or reference members, members alike) is returned in
ax by a non-member or static function, every other class through the
hidden result pointer (after 	his for member functions), also through
pointers to functions. examples/sample-abi with its cl-compiled driver
(Test 13) crosses S1/S2/S4/S8/F2/D1/S3/S12/S16 and classes with
constructors both ways, calls through pointers to functions (`(*fp)(x)`,
pointers returned by calls), variadic class arguments (passed as by-value
parameters would be) and members cl ignores for the result rule (a member
with a constructor or private data still allows `rax`; a const or reference
member, which deletes the copy assignment, does not). The remaining differences from cl's ABI
(docs/next-targets.md) are not
hit between the units of cppparse on `sample-opt`: `sandbox/scripts/mix.ps1`,
which links each cppparse object among cl-built ones, runs every
combination correctly.

**Bootstrap gate and the self-run suite**: after the checks above,
`selfhost.ps1` lets `cppparse-self.exe` compile the same 36 sources and
`libcpp/iostream.cpp` (into `out/selfobj2`), links `cppparse-self2.exe`
from them, runs it on `sample-opt`, and compares every object and `.asm`
listing of the second generation with the first byte for byte; any
difference fails the script, and the summary says how many units the
self-built compiler compiles exactly as the MSVC-built one (all 36). The
stage takes about three minutes (`-SkipBootstrap` leaves it out).
`build-cppparse.ps1 -Exe sandbox/out/selfhost/cppparse-self.exe` skips the
MSVC build and runs all test groups with the self-built compiler. That
exercised the old pipeline (`analyzer/`), folder mode, the reports and the worker
threads in self-built code for the first time and found these defects:
static locals were initialised under a plain guard byte, so two workers
could both run the initializer or use an object not yet built (the guard
is now cl's own protocol: an int guard named as cl names it,
`?$TSS<n>@...`, a COMDAT for an inline function, with the CRT's
`_Init_thread_header`/`_Init_thread_footer`, so an inline function's
static object shared with cl-compiled units is built once whichever side
reaches it first, tested by `examples/sample-statics` in Test 13; the fast
path tests the guard for 0 or -1 where cl compares it with the thread's
epoch, and an initializer that throws hands the guard to
`_Init_thread_abort` from an unwind-only cleanup, as cl does, so a later call
retries it; a static that cl initialises as a constant, `static double d =
1.0/4;`, `static int u = int();`, is static data with the bytes cl folds and no
guard, so the guards after it are numbered as cl numbers them); the
register allocator's linear intervals did not cover loop headers that only
pass a value on when the value's copies sit in edge blocks laid out after
the loop, so the header's comparison could take the value's register
(intervals now span every block where block liveness says the value is
live); equal interval starts were ordered by whatever `std::sort` did, so
the MSVC-built and the self-built compiler assigned registers differently
at `-O2` (ties now go by temporary number, and both produce identical
objects at every level); `IrBuilder::call` read the first operand of a
`new T` without operands; `ostream` rounded fixed and scientific output
through floating-point arithmetic (`132.95` with one decimal gave `133.0`,
where printf gives `132.9`) and now converts the exact binary value; and
constant expressions of 64-bit types were folded in 32 bits by sema and
the IR optimiser (`1ULL << 52` became `1 << 20`), which now fold them only
while the result fits the sign-extended 32-bit immediates and otherwise
leave the operation to run. The register, static-local and 64-bit constant
defects are reproduced in `sample-self` and checked by test 16.
Since then test 16 also checks the items fixed from
[docs/next-targets.md](docs/next-targets.md): a `float` or `double` parameter
passed on the stack is homed in its own size through xmm5, so it neither
overwrites the frame slot next to it nor the first parameter still in xmm0
(`stackFloats`, called by the cl-built driver); globals and static locals
with a destructor are destroyed after `main`, newest first: once one is
built, the module initialiser (or the guarded first pass of a static local)
registers an internal thunk `cppparse_atexit_<n>` that destroys it with
`atexit`, as cl does (the driver's output must end in the order `cba`);
a variadic function keeps its parameters in their 8-byte home slots, which
its prologue fills from `rcx`, `rdx`, `r8` and `r9` (a named `float` or
`double` from its XMM register), so the `va_start`/`va_arg` of
`include/cstdarg` walk the unnamed arguments as MSVC's `vadefs.h` does
(`sumMixed`, called by cppparse code and by the driver, and a list passed
on to `vsprintf`); with `--msvc-mangling` a static local has cl's name
(`?n@?1??counterA@@YAHXZ@4HA`: block 2 is the function's body, a C
function is `?name@@9`) and is internal unless its function is inline,
when every unit's copy is one COMDAT, so a cl-built and a cppparse-built
copy of an inline function count in the same variable (`sharedCounter`;
test 15 compares the names and linkage of `sample-mangle`'s static locals
with cl's). A static in a nested block gets cl's block number: counted
in source order from the body (2), each compound statement, `if` and each
of its branches, and each loop and `switch` takes the next one, so the
static and its guard are shared with cl's inlined copy (`nestedN` in
`sample-statics`). A global or static local whose initializer is
made of constants, string literals and addresses of globals and functions
(an array, a plain aggregate without constructors, bases or bit-fields, a
pointer) is emitted as static data, item by item, with `DQ OFFSET` for the
addresses (relocations in the COFF writer), and in `.rdata` when it is
const, as cl does: such a table is valid before any initializer runs,
whatever the link order (the driver's own initializer reads
`constantTables()` before `self.obj`'s runs).

Library calls and their result types: the version 6 front end compiles
against cppparse's own headers (`include/`), so `strtod` returns `double`
and `std::string::size` returns `size_t` like any other declared
function, and a call to a function without a declaration is error S103,
after which no object is written. All of cppparse's own units compile
this way (`selfhost.ps1`), and test 17 checks such library results
against cl's build. The old pipeline (`analyzer/`, versions 3 to 5) still
has no declarations for library functions: their results carry no type,
which, when it compiled cppparse's own sources, made the register
allocator and the instruction emitter disagree about the register class
(an XMM register with an integer operand, rejected by ml64 and reported by
the COFF writer). It compiles only part of those sources today (templates,
function-try-blocks and `std::` classes by value are skipped) and serves
the reports, not the self-build.
