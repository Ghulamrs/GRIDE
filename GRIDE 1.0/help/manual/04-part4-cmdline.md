# Part IV — Choosing a compiler, the command line, and building by hand

Everything the editor does, it does by running one of the three compilers with a
command line. This part is that command line: how the compiler is chosen, every
flag each one takes, and how to drive them yourself.

--------------------------------------------------------------------------------
## 14. How the compiler is selected

Four things decide which compiler runs, in this order of authority:

1. **The Tools menu (project/global toolchain).** If you set a specific
   compiler (`c90`, `cpp11`, `shalimar`, or `msvc`/`cl`), that wins for every group
   left at `auto`. The default is **auto**, shown with a `*` in the status.
2. **A group's own `toolchain`.** A group in the `.pro` can name its compiler,
   overriding the project's for that group only.
3. **The file's language, when everything above is `auto`.** The suffix decides:
   `.c`/`.h` → `c90`, `.cpp`/`.hpp`/… → `cpp11`, `.shl`/`.shm` → `shalimar`. This is
   the normal case: C to the C compiler, C++ to the C++ compiler, Shalimar to the
   Shalimar compiler.
4. **The Language menu**, which overrides the suffix for a file whose name lies —
   a `.txt` holding C, a `.h` that is really C++, a `.shm` the app wrote. It sets
   the colouring, the layout rules, and (through Tools ▸ By language) the
   compiler.

The resolution rule in one sentence: **a named toolchain (project or group)
wins; otherwise the language wins, and the language comes from the suffix unless
the Language menu overrides it.** The editor never sends a `.cpp` to `c90` or a
`.c` to `cpp11` under `auto`; if you force it with a named toolchain, the
compiler itself refuses by name.

`Ctrl-K` cycles the tool choice; `Ctrl-T` cycles the target; `Ctrl-D` toggles
debug/release.

--------------------------------------------------------------------------------
## 15. `c90` — command-line reference

    c90 <file.c> [more.c ...] [-S|-c] [-o out] [-D n[=v]] [-U n]
         [-I dir] [-j n] [-arch a] [-masm=m] [-g] [-time] [-nologo]

| Flag        | Meaning                                                          |
|-------------|-----------------------------------------------------------------|
| (none)      | compile, assemble and link into a program (`-o` names it, else `a.out`/`a.exe`). Several inputs link together. |
| `-c`        | stop at one object per input (named by `-o`, or after the input). |
| `-S`        | stop after this compiler; write assembly (one `.s`/`.asm` per input, or `-o` for a single one). |
| `-o out`    | name the output.                                                |
| `-D n[=v]`  | define macro `n` as `v` (or `1`); may name a target's own macro. |
| `-U n`      | undefine macro `n`.                                             |
| `-I dir`    | add a directory to the `<...>` include search (before the system `lib/`). |
| `-j n`      | how many inputs to compile at once; `-j 1` is serial.           |
| `-arch a`   | the target: `x86_64-linux`, `x86_64-windows`, `arm64-darwin`, `tms6747`. Default is the host; a non-host host target reaches `-S` only. |
| `-masm=m`   | assembly syntax for `x86_64-windows`: `masm` (ml64, default) or `gnu`. |
| `-g`        | write a DWARF line table (so a debugger can stop on a line). `x86_64-linux` and `arm64-darwin` only. |
| `-time`     | report how long each phase took.                               |
| `-nologo`   | omit the start-of-compile banner.                             |
| `--version` | print the banner and stop.                                    |

--------------------------------------------------------------------------------
## 16. `cpp11` — command-line reference

    cpp11 <file.cpp> [more.cpp ... | objects/libs] [-S|-c] [-o out]
          [-D n[=v]] [-U n] [-I dir] [-arch a] [-masm=m] [-g] [-nologo]

Its flags mirror `c90`'s, with the C++ differences:

- It accepts **`.cpp`/`.cc`/`.cxx`** (and refuses `.c` by name).
- Among its inputs it recognises **objects and libraries** (`.o`, `.obj`, `.a`,
  `.lib`) and passes them straight to the link step — they are not compiled.
- It searches **`include/` then `lib/`** for system headers (C++ then C).
- Its banner is `©2026 G. R. Akhtar - ISO C++ 11`; `-nologo` suppresses it;
  `--version` adds the sealed-version line.
- `-arch` and `-masm` behave as for `c90`; DWARF on the two GNU targets.

`-S` for a single input to standard output happens when you name `-S`, one input
and no `-o` — the editor's Assembly tab uses this.

--------------------------------------------------------------------------------
## 17. `shalimar` — command-line reference

    shalimar <file.shl> [-S] [-o out] [--target=a] [--debug]
         [-lNAME] [--no-search] [-nologo]

| Flag           | Meaning                                                       |
|----------------|--------------------------------------------------------------|
| (none)         | compile, assemble and link a program against the runtime.     |
| `-S`           | write assembly (`.s`, or `.asm` for `x86_64-windows`).        |
| `-o out`       | name the output.                                             |
| `--target=a`   | the target (note the `--target=` spelling, not `-arch`): the three host targets or `tms6747`. |
| `--debug`      | link the debug runtime (not on the emulator; Shalimar carries no line info). |
| `-lNAME`       | borrow a library, named where it lives (repeatable, in link order). |
| `--no-search`  | do not add the default library search.                       |
| `-nologo`      | omit the banner (`©2026 G. R. Akhtar - Shalimar 1.2`).       |
| `--version` / `-h` / `--help` | say which shalimar this is, or the usage, and stop. |

The one to remember: **shalimar uses `--target=x`, while c90 and cpp11 use `-arch
x`.** The editor spells each correctly; a hand-written script must too.

--------------------------------------------------------------------------------
## 18. Compiling from the command line

The compilers are in the install's `bin\`. Put it on `PATH` (the installer's
"Add the bin folder to PATH" task does this) or name them by full path.

**C, host, build and run:**

    c90 hello.c -o hello
    ./hello                        (hello.exe on Windows)

**C, several files into one program:**

    c90 main.c sum.c -o prog

**C, just the assembly for a foreign target (reaches -S anywhere):**

    c90 -S -arch arm64-darwin hello.c -o hello.s

**C++, host, with your own headers:**

    cpp11 -I include src/app.cpp -o app

**C++, object then link (what a project build does):**

    cpp11 -c -arch x86_64-windows a.cpp
    cpp11 -c -arch x86_64-windows b.cpp
    cpp11 a.obj b.obj -o prog        (objects are link inputs)

**Shalimar, host:**

    shalimar gcd.shl -o gcd
    ./gcd

**Shalimar, C6000, run on the emulator (needs the runtime beside the .s):**

    shalimar -S --target=tms6747 gcd.shl -o gcd.s
    vm6747 gcd.s bin\lib\shmrt-tms6747

**tms6747 for C/C++ on the emulator:**

    c90 -S -arch tms6747 hello.c -o hello.s
    vm6747 hello.s

**Windows note — the assembler and linker.** On `x86_64-windows`, a full build
(no `-S`) assembles with `ml64` and links with `link`, which are on `PATH` only
inside a Developer Command Prompt. The compiler finds Visual Studio itself and
sources `vcvars64.bat` for the assemble/link steps, so `c90 hello.c -o hello`
works from an ordinary shell too. If you drive the tools yourself from Git bash,
three rules bite: write `cl`/`link` options with a dash not a slash (MSYS
rewrites `/x` into a path), translate paths with `cygpath -m`, and source vcvars
in a `.bat` that then calls `bash -c` (a login shell drops what vcvars added).

**A whole project by hand.** There is no magic: read the `.pro`, compile each
group's sources with the right compiler and `-c`, then link the objects with the
C++ driver if any part is C++, else the C driver. The editor is doing exactly
this and printing each command.
