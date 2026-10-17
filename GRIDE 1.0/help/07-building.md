# 7. Building

## Two commands, and neither guesses which you meant

**`Ctrl-B` compiles the file in the edit view; `F5` runs it.** Neither asks the
project anything. A project does not have to be open, does not have to be shut,
and does not have to hold the file — open something from anywhere and `Ctrl-B`
compiles that. This is the fix-one, build-again rhythm the editor is built
around.

**`F4` builds the project; `Build ▸ Run project` builds and runs it.** These
read the file list and never the edit view. What they build is what `"build"`
says.

Which one you meant is said by which one you pressed. There is nothing here
that has to be guessed at.

## What a build shows you

The Console tab gets the command and everything the compiler said. A build of
several groups says each one as it starts:

```
$ c90, clang++ and cpp11 3 sources -o three
    src/main.c
    src/legacy.c
    engine/engine.cpp
$ Sources (c90)
$ Legacy (clang++)
$ Engine (cpp11)
$ linking with clang++
[built /home/you/three/three]
```

`Sources` and `Engine` named nothing: C went to c90 and C++ to cpp11, which is
where each goes on its own. `Legacy` is a group of C that asked for the
host's C++ compiler by name. The link is the host's, since cpp11's objects
want the C++ runtime the machine has.

**An error in a file nothing has opened opens it.** c90 stops at the first one,
and in a build of six files that is usually not the file you were looking at,
so the editor opens the one it named before putting the caret on the line and
column. With several groups the diagnostic is looked for among *that group's*
sources, so the caret lands in the file the compiler was complaining about
rather than in whichever file the target happened to list first.

## Targets

`Ctrl-T` moves to the next target; the Target menu names all four.

| | assembled and linked by | |
| --- | --- | --- |
| `x86_64-windows` | Debug: clang's assembler and `link.exe /DEBUG`; Release: RIDE's own `masm` and `link` | see below |
| `x86_64-linux` | the system's, through `cc` or `c++` | GNU assembly |
| `arm64-darwin` | Apple's clang toolchain | this Mac's own |
| `tms6747` | RIDE's own `asm6x` and `lnk6x`, against RTS6x | the TI C6747; Run runs the `.out` on `sim6747`, Build ▸ Emulate on vm6747 the assembly on `vm6747`, on any host |

**Only the host's own target reaches a program, and tms6747 reaches one
everywhere.** c90, cpp11 and shalimar generate for all four, but the three host
targets are assembled and linked by this machine's tools, so a cross target
stops at the assembly — which is shown in the Assembly tab — and the editor says
so rather than failing obscurely. The fourth target has no machine to be on: its
assembler, linker, runtime, emulator and simulator ship with the editor, so a
tms6747 program builds and runs on a Mac, a Linux box and a Windows PC alike.
[Page 6](06-the-project.md) has that target in full.

`cl`, `clang++` and `g++` take no target from this editor at all: they generate
for the machine they were installed on, so what they build is what this machine
runs whatever the Target menu says.

## What assembles and links a Windows program

Two different answers, and the configuration decides, because a debugger
decides it. The console names what ran, so a build never has to be guessed at.

**Debug.** cpp11 and c90 are given `-masm=gnu`: they write the GNU spelling of
the Microsoft ABI, **clang's assembler** turns it into a COFF object carrying
**CodeView** — the line table and the locals a Windows debugger reads — and
**Microsoft's `link.exe /DEBUG`** writes the `.pdb` beside the program, which
`cdb` reads. This is what makes a Debug build steppable on Windows (page 8), and
it is why neither RIDE's own assembler nor its own linker is in a Debug build:
`masm` carries no CodeView, and LINK writes no `.pdb`.

**Release.** cpp11 is given `-masm=masm` whenever **Tools ▸ Assembler for
x86_64-windows...** names one, which the installed `settings.json` does
(`bin/masm.exe`): it writes MASM, and RIDE's own `masm` beside it assembles. A
project's link goes to the linker **Tools ▸ Linker for x86_64-windows...** names
— `bin/link.exe`, RIDE's own LINK, in an installation; a single file's link is
cpp11's, which takes the `link.exe` beside itself for the same spelling. With
no assembler named, cpp11 writes the GNU spelling and clang assembles it, and
the link is Microsoft's `link.exe`. c90 writes MASM by default and takes the
`masm` and `link` beside it where they are.

**`ml64` is never named by RIDE.** It is Visual Studio's assembler, and it
enters a build in one way only: when RIDE's own `masm` or `link` has failed and
you say yes to the question below.

## When the project's own tools fail

When one of RIDE's own assemblers or linkers fails a build — `masm` or `link`
on `x86_64-windows`, `lnk6x` on `tms6747` — and the compilers found no fault in
the source, the editor asks:

    The project's own masm and link did not build it. Use Visual Studio's ml64
    and link.exe for this build instead?

A **Yes** builds again through the vendor's tools — Visual Studio's, found the
way `cl` is found, or TI's `lnk6x` under the directory Tools names — and the
console says what with, in the question's own words: `building again with
Visual Studio's ml64 and link.exe in place of the project's masm and link, as
asked`. A **No** leaves
the build failed. The question is put only when the vendor's tools are on this
machine (otherwise the line says they are not), only when the failure was the
tool's and not the program's (an unresolved or duplicate symbol, a library made
for another platform — the vendor's tools would refuse those the same way),
and never for a `cl` build, which links with its own. `"askNative": false` in
`settings.json` never asks; `--build` and `--run` on the console have nobody to
ask and let the build stand as failed, saying so.

**A Yes marks the program it made.** The build's output, the console and the
Output window, ends `[built with Visual Studio's ml64 and link.exe, not RIDE's
masm and link]` (`[built with TI's lnk6x, not RIDE's lnk6x]` for tms6747), or
`[not built: Visual Studio's ml64 and link.exe failed as well]` when the second
build failed too - so the record of which tools made this program stays with the
build. The file itself carries no mark.

## Debug and release

`Ctrl-D` toggles between them. What each compiler actually does about it
differs, and the editor says which rather than pretending they are the same:

| | debug | release |
| --- | --- | --- |
| `cl` | `/Od /Zi /D_DEBUG /MTd` | `/O2 /DNDEBUG /MT` |
| `clang++`, `g++` | `-g -D_DEBUG=1` | `-O2 -DNDEBUG=1` |
| `c90`, `cpp11` | `-g -D_DEBUG=1` — DWARF on the GNU targets, CodeView on `x86_64-windows`; the define alone on `tms6747`, which has no line table | `-O2 -DNDEBUG=1` |
| `shalimar` | `--debug` | nothing |

These are the defaults; **Project ▸ Compiler Options** changes them per
configuration. c90 optimises the x86-64 targets only, and its `-O2` is its
`-O1` today. cpp11 optimises every target: on tms6747 `-O1` keeps locals in
registers, schedules execute packets and fills delay slots, and `-O2` adds
software pipelining, loop-invariant hoisting and inlining. The status bar names
a toolchain that has no `-O` when you switch, rather than letting you believe
otherwise.

**`shalimar --debug` does not change the code.** The assembly is byte-identical
between the two; what changes is which runtime archive is linked, and only the
debug one has any code in it for stopping the program.

## More than one compiler in one program

Where a target's groups do not all go to the same compiler, each group compiles
to objects and the editor links them itself — because no compiler here takes an
object as an input. Hand c90 a `.o` and it reads it as C.

The objects go in a directory of the editor's own and are removed with it,
whether the link worked or not. What survives is the program.

On Windows the linker is `link` with the C runtime named, because c90's objects
carry no `/DEFAULTLIB` directive to say which one; `cl` is given `/MT` there to
match. Everywhere else it is the same host driver c90 hands its own linking to.

## How a build runs, and what runs at once

A project build compiles one group at a time, in the order `"build"` names
them, and each group is one command to one compiler with all of its sources
on the line. What happens inside that command is the compiler's:

- **cpp11 compiles its files on a pool of threads** — one per file up to the
  machine's cores, and one thread for fewer than four files — and says so on
  its first lines: `6 source files, 6 compilation threads`, then `[thread 2]
  parser.cpp` as each thread takes a file. The editor passes no `-j`, so the
  count is cpp11's own; on a command line `-j n` asks for another, and `-j 1`
  is serial. Assembling goes on the same pool. c90 and shalimar compile one
  file after another.
- **A tms6747 group compiles on RIDE's own pool.** Each source is its own
  `-S -arch tms6747` command - the drivers write one `.s` per input and take
  no output directory - and RIDE runs them at once, one thread per source up to
  the machine's cores; each one's output is captured and written to the
  console in source order as it completes, and the first failure in that order
  is the build's. The `asm6x` step that follows takes every `.s` in one
  command, and the link is one `lnk6x`.
- **The link is one command**, and the run is one program; nothing overlaps a
  build with a run or two builds with each other. The window keeps drawing
  while a build runs, because the build is on a worker thread of its own —
  that is the only concurrency in the editor itself.

## What stands behind each tool

The compilers and tools a build goes through are RIDE's own, and each is held
to an outside oracle rather than to itself. The short form, so a reader of
this page knows what a green build rests on; the long form is each
repository's README and, for cpp11, `docs/` beside its sources:

| | held to | by |
| --- | --- | --- |
| c90, cpp11 | clang (`-std=c++11 -pedantic-errors`) for every recorded output and every linkage name; cl for the Microsoft ABI | the compilers' suites on three machines (`tools/verify-three`) |
| cpp11 on tms6747 | TI's own assembler, linker and the CCS 5.5 cycle-accurate simulator | `tools/verify-three c6747`, a scheduled job on the Windows box |
| masm, link | Visual Studio's `ml64` and `link.exe`, object and image byte for byte | their probe beds — and not yet by cpp11's own suites, which assemble the GNU spelling; the MASM spelling a Release build goes through has been proven by hand runs so far |
| asm6x, lnk6x | TI's `asm6x` and `lnk6x`, object and image byte for byte | their probe beds; lnk6x's known differences pinned by name |
| RTS6x | TI's `rts6740` on sim6747 and on CCS 5.5's simulator | `make check` |
| sim6747 | CCS 5.5's C6747 simulator, output and cycle count | its suite |
| vm6747 | the host targets' output for the same program | the compilers' suites — see the note on page 6 about what it cannot see |
| RIDE | its own suites: `tests/test`, `tests/session`, `tests/toolchain-check` | on all three machines |
