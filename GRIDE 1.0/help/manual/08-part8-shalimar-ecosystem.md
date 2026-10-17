# Part VIII — The Shalimar ecosystem and the c2s converter

Shalimar is more than the `shalimar` compiler inside RIDE. It is a small family of
projects that share one language, and the C↔Shalimar converter (`c2s`) that
RIDE ships is the bridge between the C world and the Shalimar one. This part
maps the family, then documents `c2s` in full — because `c2s.exe` is in your
install and its Language-menu items are one keystroke away.

--------------------------------------------------------------------------------
## 31. The map: what is inside RIDE, and what is beside it

It is worth being exact about which Shalimar things RIDE *is*, and which are
separate projects that share the language:

**Inside RIDE (what you install):**

- **`shalimar`** — the Shalimar compiler (the i-line of Compiler-S), four targets.
  This is what F5/F4/Run project use for a `.shl` file.
- **`shmrt-*`** — the Shalimar runtime, in `bin\lib\` beside `shalimar`.
- **`c2s`** — the C89↔Shalimar source converter, driven from the Language menu.

**Beside RIDE (separate projects that share the language):**

- **Compiler-S** — the upstream Shalimar native compiler that `shalimar` is built
  from. C++14, three host targets; RIDE's `shalimar` (since 3.5) adds `tms6747`.
- **The Shalimar app family** — the iOS apps whose Swift interpreter *runs*
  Shalimar on a phone. These are not compilers and are not part of RIDE; they
  are where the language's own specification lives, and RIDE's Shalimar help
  is a checked copy of it. Three of them:
  - **Shalimar** — the original iOS app: a Swift/UIKit editor and interpreter.
    It is the authority for the language (`SHALIMAR_LANGUAGE.md`), and it is
    sealed — RIDE never opens or changes it.
  - **Shalimar-2** — a newer iOS app that runs **C** on a phone by converting it
    with `c2s` to Shalimar and interpreting the result: `C source → c2s →
    Shalimar → the interpreter already in the app`. iOS only, no code generation
    — `c2s` is the only native code, and it carries its own C front end so it
    needs neither `c90` nor `shalimar`.
  - **Shalimar-3** — a further iteration of the app line, reviewed and hardened
    (for example, a character-count output ceiling beside the line ceiling, and
    not leaking the parse tree on a refused compile).

The relationships that make them look like one project but do not make them one:
Compiler-S records its expected output *from* the app's interpreter (the
interpreter is the oracle, the document is the authority); `c2s` *vendors*
Compiler-S's Shalimar front end so the two agree on what Shalimar accepts; and
`shalimar` is Compiler-S one target on. RIDE ships `shalimar` and `c2s`; everything
else in this list is upstream or a sibling.

--------------------------------------------------------------------------------
## 32. Compiler-S, `shc` and `shalimar`

**Compiler-S** is the native Shalimar compiler. It compiles `.shm`/`.shl`
programs to native assembly for `arm64-darwin`, `x86_64-linux` and
`x86_64-windows`; RIDE's **`shalimar`** (since 3.5) is the same compiler with `tms6747`
added. It is C++14, held to the same `-Wall -Wextra -Werror -pedantic`
discipline as the C and C++ compilers.

**The runtime.** A Shalimar program is not self-contained: it links against a
runtime archive (`shmrt-<target>`) that owns `main`, formats numbers, does the
console I/O, manages arrays and carries the failure paths. The compiler looks
for that runtime in `lib/` beside its own binary and then in `../lib` — which is
why the install puts `shmrt-*` in `bin\lib\`. A `shalimar` that stands without its
runtime compiles and writes correct assembly and then fails at the link, which
is why the editor's `confirm` step checks the runtime is present, not just the
compiler.

**The oracle.** Compiler-S records its expected results from the Shalimar app's
interpreter and keeps every divergence in its `docs/CONFORMANCE.md`. The one
that exists is small and worth knowing: the language document says `t : s` copies
a string; the app's interpreter aliases it; the compiler copies (matching the
document, which is the authority). So a Shalimar program that mutates a string
after an assignment can behave differently under the interpreter and the
compiler — the document, and the compiler, are the reference.

**Debugging.** Shalimar carries no debug information on any target, by decision.
The Debug menu turns a Shalimar file away by name.

--------------------------------------------------------------------------------
## 33. The `c2s` converter — what it is

`c2s` converts source between **C89 and Shalimar, both directions**:

    C89 source   →  c2s  →  Shalimar source     (CToS)
    Shalimar     →  c2s  →  C89 source          (SToC)

It is a source-to-source translator, not a compiler: it never generates machine
code. It has its **own C front end** (its own lexer, parser, preprocessor and
pre-scan — about 1,000 lines, sharing only idiom with `c90`) and it **vendors
Shalimar's front end** from Compiler-S, so it agrees exactly with `shalimar` about
what Shalimar accepts. In the editor, two Language-menu items put `c2s` over the
open file (C→Shalimar, and back). From a shell, `c2s.exe` takes the file and a
direction.

The two directions have different characters:

- **C → Shalimar is mostly a *rejection* problem.** C is the larger language;
  the converter's job is to carry what maps faithfully and to **refuse, by name,
  anything it cannot** — never to translate something into Shalimar that means
  something subtly different.
- **Shalimar → C is mostly a *runtime* problem.** Shalimar's numeric behaviour
  (its integer-overflow trapping, its printing) has no plain C form, so this
  direction emits a small `c2s_*` C preamble that reproduces Shalimar's
  behaviour, and the converted C carries it.

--------------------------------------------------------------------------------
## 34. `c2s` features — C → Shalimar, in detail

**Control flow it lowers faithfully.** Shalimar has no `goto` and no labels (the
spec says so three times), so C control flow is expressed with what Shalimar
has:

- **`switch`** becomes an `if`/`elseif`/`else` chain when every case breaks.
- A **`break` from the middle of an arm** is carried by wrapping the chain in
  `while 1 { … break }`, so C's `break` binds to a wrapper that holds exactly the
  switch — leaving the wrapper and leaving the switch are the same jump. (A
  `continue` in such an arm would bind to the wrapper too, so that one
  combination is refused; either alone converts.)
- **Real fall-through** becomes an entry-index / done-flag shape that reproduces
  C's rule exactly, a `default` in the middle of the list included.
- A **grouped label** (`case 4:` immediately followed by `default:`) is one arm
  and gets no fall-through machinery.
- **`for`, `while`, `do-while`, `if`/`else`** all carry; the **counting `for i :
  a to b`** is preserved wherever the loop counter does not escape — this is what
  makes the Shalimar output readable, and the converter protects it across all
  three switch shapes.

**`printf` becomes a generated function per format** — `print_1(a1: int)` and a
call — because Shalimar evaluates a call's arguments before entering, which is C's
evaluation order at a `printf`. Formats are shared when their text and parameter
types match. The format specifiers, each verified by running the converter:

    carried:  %d  %i  %s  %c  %f  %.Nf  %%          (N up to 17)
    refused:  %g  %e  %x  %ld  %u  %.3d
    refused:  %4d   %-5d   %8.2f     (a WIDTH or a FLAG)

A **width** is the specifier that catches people — `%4d` is how a C table is laid
out and looks like nothing special, but Shalimar's `?` has no field width, so it
is refused; the format must carry its own spaces. A **precision** carries only on
`f` (`%.Nf` is `prec(N)`); `%.3d` is zero-padding and `%.3s` a truncation,
neither of which `?` has.

**The `?` spacing caveat, said out loud.** Shalimar's `?` writes a space after
every item, so a format whose text runs against a hole gains one space:
`printf("value %d.\n", n)` can only become `? "value" n "."`, which writes
`value 5 .`. There is no string concatenation and no number-to-text builtin to
build the line as one item, so this is carried **with a warning per `printf`
naming the line**, rather than refused — a program that prints one extra space is
still the program, but the difference is never left unsaid. (The differential
tests for this *remove* every space from both sides before comparing, because the
space is one the C never wrote.)

**Integer overflow is carried across.** Shalimar traps where C89 wraps, so int
`+ - *` go through `c2s_add_int` and friends, which stop with the runtime's
message, on its stream, with the statement's line — rather than silently wrapping
as C would.

**Constructs it carries vs refuses** (each measured, not read off the source):

    carried:  double/float (→ real), i++ as a statement,
              x = i ? 2 : 3 as a whole right-hand side
    refused:  long, short, unsigned, static inside a function, goto,
              ++/-- inside an expression, ?: inside a larger expression

So several refusals are about **where**, not what: `x = i ? 2 : 3` converts but
`x = 1 + (i ? 2 : 3)` is refused; `i++;` converts but `a[i++]` is refused.

**Permissions (`--pragmatic`, three of them).** A *permission* guards a rewrite
that would compile but not mean *quite* what the C did — so it is off by default
and asked for explicitly:

- **`--allow-short-circuit`** — `&&`/`||` become Shalimar's `&`/`|`, which ask
  both sides; on plain variables `x = a && b` converts with no flag, but where a
  side has an effect the flag is required.
- **`--allow-...`** narrowing — an `unsigned` narrowed to Shalimar's number loses
  the range it had.
- **char arithmetic** — carried where safe.

`--pragmatic` turns the set on. A rewrite that reproduces C exactly (the switch
lowerings) is **not** a permission — it costs readability, not correctness, and
only risks are asked about.

**Header guards and macros.** A guard that holds nothing but a definition —
`#ifndef M_PI / #define M_PI 3.14 / #endif` — is dropped with a comment, because
it decides nothing. But in a file with any `#include`, that same drop is a
**warning**, because the included header might define the name to something else
(a real `<math.h>` `M_PI` is the full pi, not `3.14`). `__LINE__` and `__FILE__`
are expanded (they are what `c90` supplies); other conditionals stop the run,
because they ask which program this is and the file does not say.

**Diagnostics and the line map.** A construct it cannot carry is reported as a
`ConversionError` (**code C2100**) with a line and column, so the editor can put
it in the margin — not only as a marker in the output. And `--lines` prints a map
of one C line per line of Shalimar written (`out: in` pairs), so a converted
program can be traced back to its source.

--------------------------------------------------------------------------------
## 35. `c2s` features — Shalimar → C

This direction emits C89 that a `-std=c89 -Wall -Wextra -pedantic -Werror`
compiler accepts, and it carries a small `c2s_*` preamble to reproduce Shalimar's
runtime behaviour (numeric formatting, overflow trapping). It gates the preamble:
a program that prints only integers does not carry the floating-point globals it
never reads, so its output does not open with warnings of its own (this
direction's output *leaves* — it goes to somebody else's terminal).

It straightens the shapes so the C reads well: a Shalimar `elseif` chain becomes
a flat C `else if` chain (not a staircase), and an `else` holding a lone `if`
collapses to `else if` — but an `else` block with a *second* statement keeps its
braces, because `else { if (c) A B }` and `else if (c) A B` are different parses,
not different layouts.

**Its own boundary.** Where a Shalimar construct has no faithful C form it is
refused with the same C2100 machinery. And one difference is the *language's*, not
the converter's: Shalimar folds `-x` into `0 - x` for a non-literal operand, so
`-0.0` comes back `+0.0` — writing `-x` by hand in Shalimar does the same. The
converter is faithful; the behaviour lives in the language.

--------------------------------------------------------------------------------
## 36. Using `c2s` from the command line

`c2s.exe` is in the install's `bin\`. It converts the file it is given; the
Language menu drives it in the editor, and by hand:

    c2s program.c              C → Shalimar (writes the .shl/.shm form)
    c2s program.shl            Shalimar → C (writes the .c form, with the preamble)
    c2s --pragmatic program.c  allow the meaning-changing rewrites
    c2s --lines program.c      also print the out:in line map on stderr
    c2s --canon program.c      print the parsed C back as C (the lowered form)

Because `c2s` only ever *runs* `c90`/`shalimar` for its differential tests and never
builds or edits them, it works even while those compilers are otherwise busy, and
it is the safe way to move a program between the two languages: it carries what
it can prove faithful and refuses — by name, with a line — what it cannot.
