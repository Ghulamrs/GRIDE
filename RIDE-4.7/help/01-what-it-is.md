# 1. What it is

RIDE is an editor for three languages of our own: **C** through
c90, **C++** through cpp11, and **Shalimar**
through shalimar — with the machine's own C and C++ compiler,
`cl` on Windows and `c++` elsewhere, reachable by name for the first two. It
edits, builds, runs and debugs, and it does all four without leaving the
keyboard.

It is not a general-purpose editor that happens to know some compilers. It was
written for c90 and grew the other two, and that shows in what it does well: a
diagnostic puts the caret on the line, a build shows you the assembly it
produced, and a breakpoint works the same whichever of the three you are in.

## Three variants, one core

| | runs on | what it is |
| --- | --- | --- |
| `RIDE.exe` | macOS, Linux | the terminal editor |
| `RIDE.exe` | Windows | the same editor, in a window |
| `RIDEConsole.exe` | Windows | and over the Windows console |

**Every rule lives in `src/` and all three call it.** Laying a line out,
colouring it, reading the project file, choosing a compiler, driving a debugger —
one implementation each. Only `editor.cpp` with `terminal*.cpp`, and
`winforms/`, are specific to a front end.

That is not tidiness for its own sake. Two editors that behave nearly the same
are worse than one editor with two windows: the "nearly" is where the bugs
live. When something is asked for in one, the answer is to lift it into the
core and rewire the other.

The names `RIDE` and `RIDEGui` belong to the binaries. **RIDE** is what the
pair is called, and it is what `Help ▸ About` prints.

> Called *CC1 Studio Workbench* until 2026-08-22, when Shalimar became the
> third language and the old name stopped describing it.

It is also not **CC1 Studio**, which is a different thing for the same
compiler: that one is an extension that teaches VS Code about c90, and this is
an editor of our own.

## Releases

**1.0 — the editor for C and C++.** cc1 and `cl`, the three targets, the
project file and its groups, the panel with its three tabs, and real debugging:
breakpoints, stepping, variables and the call stack. Complete in itself, and
what the name *CC1 Studio Workbench* described.

**1.1 — Shalimar.** A third language. Shalimar is not C with fewer rules, so
it did not arrive as a suffix in a table: it brought its own indent dialect,
because `n : n + 1` is an assignment here and a label there; its own way of
finding the rest of a program, because it has no `include`; and its own
debugger, because a Shalimar program stops itself and there is nothing to
install.

The same release moved the compiler from being a property of the *project* to
being a property of a **group**, so a target can hold C and C++ together —
which is what made C the only language with a decision in it. It also gave C++
a compiler off Windows, which it had never had: `clang++` on a Mac and `g++` on
the Linux box, where `auto` used to route C++ to a `cl` that was not installed.

And it is why the product is called RIDE. Three languages is where *CC1
Studio Workbench* stopped being a description.

**1.2 — Shalimar borrows, and can call C.**

Shalimar stopped having a library of its own. `sin` and its nineteen
neighbours were available to every program whether it wanted them or not, and
now a file asks — `uses sin, cos` — which costs no program a name it did not
spend, and let the borrowable set grow from twenty to twenty-seven without the
runtime archive growing by a byte, because a borrowed function is a direct call
into the platform's own libm.

The same form reaches further. `uses <real> = mean(a[]: real)` declares a
function **this compiler will never see**, provided by a library the link is
given with `--with=` — so a Shalimar program can call C compiled by `c90`, by
`cl`, or by the host's compiler. Arrays cross too, as an opaque handle, which
meant writing the array ABI down at last rather than leaving it as whatever two
functions happened to agree on.

The one thing it costs: a program that declares a foreign function has no
interpreter, because the phone app has no link step. Both readers parse the
declaration and the app refuses it by name.

[Calling C from Shalimar](mixing-c-and-shalimar.md) is the page; the worked
examples are in the two compilers' own repositories.

**3.0 — cxx1.** The release this manual is for. A fourth compiler, and the
second language of our own that has one: C++ goes to **cpp11**, the C++11
compiler that grew out of c90, the way C goes to c90 — by default, on every
machine alike, with the same targets and DWARF on the same two of them.
The host's C++ compiler is not gone; it is what a group asks for by name now,
which is exactly the position the host's C compiler has always been in. So C
stopped being the only language with a decision in it, and the two decisions
are the same one.

What it changed around the edges: a project of C and C++ builds with nothing
named and neither half going to the host; the Debug tab reads cpp11's DWARF
through the same lldb or gdb it reads c90's; `Help ▸ About` lists four
compilers, one per line, because cpp11's `--version` is two lines long; and
the workspace on each machine builds five programs rather than four.
[C++](cpp.md) is the page.

**3.5 to 5.1 — the fourth target, and the tools that make it one.** Since 3.5
the three compilers generate for a fourth machine, **`tms6747`**, TI's
TMS320C6747 DSP, and the editor builds and runs for it on every host — which
no host target can say. What makes that possible is the other half of the
product, twelve sealed programs in all: **`asm6x`**, the C6000 assembler,
**`lnk6x`**, the C6000 linker, and **RTS6x**, the C6747 run-time library a
program links against by default, all RIDE's own; **`vm6747`**, an emulator
that runs the compiler's assembly as it is, with a C library and an
exception-handling runtime of its own inside it; and **`sim6747`**, a
simulator that runs the linked `.out` — TI's own boot code and runtime, the
machine code asm6x encoded, the image lnk6x laid out. `F5` and **Run project**
build that `.out` and run it on the simulator, so asm6x, lnk6x and RTS6x are on
every run's path; **Build ▸ Emulate on vm6747** runs the assembly on the
emulator, and **Build ▸ Verify** runs both and compares them. On Windows the same releases brought RIDE's own
**`masm`** and **`link`** for Release builds and, in 5.1, debugging of c90 and
cpp11 programs through CodeView and cdb. A Code Composer Studio project opens
as it is. [Page 6](06-the-project.md) and [page 7](07-building.md) have all of
it; `docs/ride-architecture.html` is the picture.

## What it will not do

Said here so that the rest of the manual does not have to keep apologising.

- **It does not have a plugin system**, a package manager, or a settings UI.
  Configuration is one JSON file per project and one per machine.
- **It does not guess.** Where two things could be meant, it asks or refuses,
  and the refusal says which file to move or which line to change.
- **It stops at the assembly for a cross host target.** Building for a Mac,
  a Linux box or a Windows PC you are not on produces assembly and nothing
  else, because the assembler and linker it hands off to are this machine's.
  `tms6747` is not a cross target in that sense: its tools travel with the
  editor.
- **It has no optimiser of its own, and does not want one.** The editor
  optimises nothing; what a Release build gets is the compiler's — cpp11's
  `-O1` and `-O2` on every target, c90's on the x86-64 targets, nothing from
  shalimar — and [page 7](07-building.md) says what each does.
- **It does not put a program on a C6747 board.** The `.out` it links is the
  file Code Composer Studio loads onto one; the loading is CCS's.
