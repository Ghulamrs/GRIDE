# Part XIII — Diagnostics, exit codes, and how the tools report

Every tool here says what it did on its standard streams and leaves with a status
you can test. This part is how to read those reports and what the exit codes mean —
useful when scripting the compilers, and when a build in the editor stops.

--------------------------------------------------------------------------------
## 65. The shape of a diagnostic

A compiler diagnostic names the **file, line and column**, the **severity**, and a
**message**:

    shape.cpp:4:5: error: expected a type
            virtual ~Shape() {}
            ^

The caret points at the column. The editor turns the top diagnostic into the
status line and, on a double-click or Enter on the Console, jumps the caret
there. Two principles govern every message:

- **Diagnosed at the point of interception.** The error is reported where the rule
  was broken, not deferred to a later phase, so the line and column are the real
  site.
- **Refused by name.** An unsupported construct produces a *specific* message, not
  a generic parser stumble. A program that compiles stepped on nothing; a program
  that fails names the reason.

--------------------------------------------------------------------------------
## 66. Messages you will actually meet

- **`c90: <file>.cpp looks like C++ (.cpp), and c90 compiles C, not C++ — compile
  it with cpp11`** — the C compiler was handed a C++ file. Use `cpp11`, or let
  `auto` route it.
- **`cpp11: <file>.c looks like C (.c), and cpp11 compiles C++, not C — compile it
  with c90`** — the mirror.
- **`<arch> only reaches -S here — switch to <host> to run it`** — a foreign
  target: assembly is produced, this host cannot assemble/link it.
- **`… has no <arch> target — it is c90's, cpp11's and shalimar's`** — you asked a
  compiler for a target it does not have (e.g. the host compiler for tms6747).
- **`-g asks where each line went, and this compiler writes no such thing for
  x86_64-windows in the MASM spelling …`** — `-g` on the MASM path; use
  `-masm=gnu` or build the C++ with `cl`.
- **cpp11 "… is not supported yet" / "… is C++14, and this compiler is C++11"** —
  a documented exclusion (Part X).
- **c2s `C2100` with a marker** — a construct the converter cannot carry
  faithfully; the message names what to write instead.
- **`'ml64.exe' is not recognized`** — the assemble step ran without a Visual
  Studio environment (Part V chapter 22); normally the compiler sources vcvars
  itself.
- **`RIDE is in <bin> without what it drives`** — the editor's `confirm`: the
  editor is present but a compiler or the runtime is missing beside it.

--------------------------------------------------------------------------------
## 67. Exit codes

The rule the compilers follow: **a question answered is not a failure.**

| Code | Meaning                                                               |
|------|----------------------------------------------------------------------|
| 0    | success — or a question answered (`--version`, `--help`) leaves with 0. |
| 1    | a compile/build error (a diagnostic was issued), or a bad argument for c90/cpp11. |
| 2    | shalimar's bad-argument code; also a suite/tool "an oracle is missing" exit (it exits 2 rather than passing when a required binary is absent). |
| 3    | ti-build: TI CGT not found, or the EH runtime could not be built.     |

Two scripting traps worth stating:

- **Take the status from the compiler, not the end of a pipeline.** `x=$(bin … |
  tr -d '\r'); st=$?` is `tr`'s status (always 0). Redirect to a file, take `$?`,
  then filter the file — otherwise an `err_` case that must exit 1 reads as a
  pass.
- **`--version` and `--help` exit 0** on purpose, so a script asking three
  compilers their versions does not stop at the first.

--------------------------------------------------------------------------------
## 68. Reading a build in the editor's console

A file build (`Ctrl-B`) shows the one command and its output. A project build
(`F4`/Run project) shows a header naming the compilers and the sources, then, per
part, the compiler's own output — for a mixed target, `Sources (c90)` and its
assembling lines, then `Sources (cpp11)` and its banner, then `linking with …`,
then `[built <program>]` or the first error. Each compiler prints its banner at
the start of a compile (unless `-nologo`), so the console names the compiler that
produced each object — which is how you tell, at a glance, that `main.c` went to
`c90` and `shape.cpp` to `cpp11`.

The bottom panel has three tabs: **Console** (the build and the program's output),
**Debug** (the debugger, where the target supports it), and **Assembly** (the
compiler's `-S` output for the current target, coloured). `Ctrl-1`/`2`/`3` switch
them; on Console, Enter on a diagnostic line jumps to it.

--------------------------------------------------------------------------------
## 69. When a whole suite fails on one machine

A pattern worth recognising before you suspect the compiler: **a total failure on
Windows only is usually a line-ending question.** MSVC writes CRLF on stdout;
golden files are LF; a naive comparison then reports every case failed while the
compiler is entirely correct. Normalise with `tr -d '\r'` before comparing. The
same shape — "everything fails on one host" — is nearly always the host's tools
or conventions, not the code that was the same on the other two.
