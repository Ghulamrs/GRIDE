# C++

C++ goes to **cpp11**, the C++11 compiler that grew out of c90, and has had
the same shape as C since 3.0: the editor's own compiler by default, and the
machine's own — `cl` on Windows, `clang++` on a Mac, `g++` on Linux — when a
group asks for it by name. Until 3.0 C++ had no decision in it and went
straight to the host's compiler; that compiler is still there, one Ctrl-K
away, and every project written for it still builds.

| | |
| --- | --- |
| suffix | `.cpp`, `.cc`, `.cxx` |
| compiler | `cpp11` by default; `cl` or the host's `c++` when a group says so |
| targets | `x86_64-windows`, `x86_64-linux`, `arm64-darwin`, `tms6747` — the same four as c90 |
| debug | `-g -D_DEBUG=1` — DWARF on two targets, CodeView on `x86_64-windows`; the define alone on `tms6747`, which has no line table |
| release | `-O2 -DNDEBUG=1` — cpp11 optimises every target; see [page 7](07-building.md) |

## Where cpp11 is found

`--cpp11`, then `$CPP11`, then a `cpp11` beside the editor, then PATH — the same
four steps as c90, and the same rule about naming one that is not there: the
editor drops it with a word and carries on as if nothing had been named.

cpp11 carries its own standard headers, in `include/` and `lib/` beside its
binary or one directory above it, and falls back to the paths compiled into
it. A copy that is moved on its own still finds the checkout it was built
from; `make product` and `build.bat product` copy the two directories so that
a product does not depend on one.

## What cpp11 reads

C++11, as a subset on purpose — the language cpp11 accepts is not the one it
is written in, and its own README says which. Two things a C++ file may
reasonably hold that it refuses today are `= delete` on a member and a
`static const int` member used as an array bound in its own class; the
examples here spell both the older way, which every compiler reads. A file
that needs more than cpp11 has is a file for the host's compiler, and Ctrl-K
is how it gets there.

cpp11 announces itself on every compile — one line on standard error before
the work starts — and the Console tab shows it above whatever else the
compiler said, in both front ends. That is cpp11's own behaviour and the
editor does not edit it: c90 and shalimar say nothing on a compile, and cpp11 says
who it is, and the console reports each as it is.

## The host's compiler, by name

**By name, not as "c++".** The console says which compiler ran — `(clang++)`
on a Mac, `(g++)` on the Linux box, `(cl)` on Windows — because which one it
is *is* the information, and the generic alias tells a reader less than the
machine already knows. `--cxx` or `$CXX` names another; a project file never
does, because which C++ compiler a machine has is a fact about the machine.
A group that wants it says `"toolchain": "c++"`, and a group that wants cl
on Windows says `"msvc"`.

> This used to route to `cl` on every machine, which meant a C++ file on a Mac
> was sent to a compiler that is not installed there and never could be. A
> project of C and C++ could therefore only ever have been built on Windows.

## Finding cl

`--cl`, `$CL`, or Visual Studio 2022 itself — the editor imports the
environment a Developer Command Prompt would have, so it works from an ordinary
console. The search is pinned to 2022; a bare "latest" would reach past it to a
newer Visual Studio, which is not the toolset this is built with.

## Debugging

**cpp11 writes DWARF for `x86_64-linux` and `arm64-darwin`** — line tables,
types, objects and lexical blocks — and lldb or gdb read it like c90's, so
breakpoints, stepping, locals and the stack all work.

**On `x86_64-windows` it writes CodeView**, since RIDE 5.1: a Debug build is
given `-masm=gnu`, clang assembles the object with its line table and its
procedure and local records, Microsoft's `link.exe /DEBUG` writes the `.pdb`,
and **cdb** stops on a line, steps, shows locals and walks the stack — the same
debugger that reads a `cl` build. The MASM spelling, which a Release build
uses through RIDE's own `masm`, carries no line table; that is why Debug and
Release go through different assemblers on this target ([page 7](07-building.md)).
On `tms6747` there is no line table and no debugger: the Debug tab says so.

**cl writes CodeView into a `.pdb`, and `cdb` reads one.** cdb comes with the
Windows SDK's debugging tools and is not installed by default, so the editor
looks for it rather than assuming — and says *"cl writes a .pdb and cdb reads
one, but cdb is not installed"* when it is missing.

`clang++` and `g++` write DWARF and are read by lldb and gdb like anything
else.

So on Windows, C++ under cpp11 and C under c90 are debugged by cdb through
CodeView, exactly as C++ under cl is. Which debugger applies is a fact about
the compiler and the target, not about the machine, which is why the editor
asks `debuggerFor(compiler, target)` and never `debuggerFor(machine)`.

## Beside C in one program

A target may hold both. Each group compiles to objects with its own compiler
and the editor links them — see [page 7](07-building.md). The ordinary case
is now c90 and cpp11 side by side, with nothing named in the project file at
all, and the host's linker joining what the two produced.

One thing the editor has to arrange for you on Windows: **cl is given `/MT`**
there, because c90's own driver links `libcmt` and two C runtimes in one
program is `LNK4098` at best and two heaps at worst. Nothing else is in a
position to make them agree.
