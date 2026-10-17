# Shalimar

Shalimar is a small numeric language of our own, interpreted by an iOS app and
compiled by **shalimar**. The full specification is
[Appendix A](appendix-a-shalimar-language.md).

| | |
| --- | --- |
| suffix | `.shl` |
| compiler | `shalimar` |
| targets | `x86_64-windows`, `x86_64-linux`, `arm64-darwin` |
| debug | `--debug` — links a runtime that can stop, changes no code |
| release | nothing |

`.shl` is the only suffix this editor reads as Shalimar. The phone app writes
`.shm` and every program it ships is one, and that suffix was read here too
until 2026-08-23 — it was dropped because `.shm` is not accepted everywhere a
Shalimar file has to go, and one name that travels is worth more than two that
do not. An app-written `.shm` opens as plain text; `Language ▸ Shalimar` reads
it as Shalimar without renaming it, and renaming it to `.shl` is the permanent
answer.

## Laying it out is not the C rules

`n : n + 1` is an **assignment** in Shalimar and a **goto label** in C, and a
label is laid out in its function's own column. A Shalimar program indented by
the C rules therefore walks left one statement at a time. `IndentDialect`
exists for exactly that, which is why the Language menu matters here.

Shalimar also has **no block comment, no escapes in a literal, no character
literal and no preprocessor**. The colouring knows all four.

## The library, and asking for it

Shalimar has no maths functions of its own. `sin` and its neighbours are the C
library's, and **a file may call one only if it says so**:

```
uses sin, cos, tan
uses fmod
```

Global space, several to a line, per file. A file that does not borrow `fmod`
may use `fmod` for a variable of its own.

| | |
| --- | --- |
| powers and roots | `sqrt` `cbrt` `pow` `hypot` |
| logs | `log` `log10` `log2` `exp` |
| trigonometry | `sin` `cos` `tan` `asin` `acos` `atan` `atan2` |
| hyperbolic | `sinh` `cosh` `tanh` |
| rounding | `round` `ceil` `floor` `trunc` |
| the rest | `abs` `max` `min` `fmod` `len` |
| constants | `pi` `e` — values, not calls, and not borrowed |

Twenty-seven, and asking is what lets there be that many: making them all
available by default would take twenty-seven names from every program that
wanted none of them. Adding more costs the runtime archive nothing, because a
borrowed function is a direct call into the platform's own libm rather than a
wrapper.

`fmod(x,y)` and `x % y` are the same operation on reals. The operator needs no
borrow, so it is the shorter way to say it; the function is there for a program
that would rather name it.

The same keyword reaches a **C library you wrote** — see
[Calling C from Shalimar](mixing-c-and-shalimar.md).

## No include, and none coming

A call to a function this file does not define is **looked for in the project's
other files**, and what is found is compiled in. There is nothing to declare and
nothing to keep up to date.

- Only functions are looked for. A brought-in function carries its own file's
  globals — the ones it actually reads — and no others.
- Nothing arrives that was not asked for, so a `main()` is never pulled in and
  a directory full of programs is the ordinary case rather than a collision.
- A name **something wants** in two files is refused, naming both. A name
  nobody wants may be in as many files as it likes.

**The project is what says which files those are.** Given one file on a command
line, shalimar looks beside it; given a project, the editor hands it the group, with
the program first. A directory knows what is next to it and a project knows
what is its own.

Since every Shalimar file has a `main()` and nothing inside a file can say it
is the program, **`"target"` in the project file picks which one it is**. A target named
after none of them is refused rather than guessed at.

## A program that stops itself

There is no debug information — no `-g`, no DWARF, no CodeView — and none is
planned. What there is instead is better for this language: the compiler
already emits `shm_line(unit, line)` before every statement so a runtime error
can name where it happened, and a **debug build offers that same position to a
session inside the program**.

So `F9`, `F8`, `F7` and `F6` work on Shalimar with no debugger installed
anywhere, on all four targets — including `x86_64-windows`, where c90's own
debugging stops, and `tms6747`, where a Debug build's `.out` runs on `sim6747`
and its session talks to the editor through the simulator. **In the window as well as in the terminal**, which makes
Shalimar the only language the window can stop on the machine it runs on.

What it cannot do is read a variable: the compiler emits no table of a
function's names against its frame slots, and the Debug tab says so rather than
showing an empty list. Watching an expression and walking the stack are refused
for the same reason and in the same words, and in the window those three items
of the Debug menu are drawn faint while a Shalimar program is stopped rather
than offered and then refused.

**A release build cannot be stopped at all.** That is the boundary, not a
limitation of it: the release runtime has no code in it for stopping. Rebuild
in debug — it is the same source and byte-identical compiler output.

## Why a Shalimar object cannot be one group among several

It is refused twice, and the two refusals say different things.

**In one group with C or C++** — no compiler takes both, so naming one cannot
help. The message names the group, because splitting the list is the fix.

**In a group of its own beside them** — where every other pair of languages now
builds. This one is about what a Shalimar object *is*:

- every unit exports `shm_user_main`, `shm_init_globals` and `shm_name_files`
  whatever file it came from, so two of them collide by construction;
- the runtime archive owns `main`, so a C program with its own cannot link it,
  and a Shalimar object without the runtime has nothing to call;
- and until 1.2 the language had no declarations, so a call across a link could
  not have been checked.

The first two are mechanical. The third **stopped being true**: `uses <real> =
f(a[]: real)` declares a function the link provides, and is checked exactly as
a written one is. `Compiler-S/docs/LINKING.md` has the actual linker output and
what would still have to change.

**So the two halves of this are no longer the same question.** A Shalimar
*object* still cannot be one group among several — that is the collision above,
and it is why a project wanting Shalimar beside C builds two programs. But a
Shalimar *program* can call into C perfectly well, because the C arrives as a
library rather than as a peer, and only one Shalimar unit is ever in the link.
[Calling C from Shalimar](mixing-c-and-shalimar.md) is that page.
