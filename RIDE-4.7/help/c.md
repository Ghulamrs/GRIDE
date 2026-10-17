# C

C goes to **c90**, the compiler this editor was written for. It is the only
language with a real choice in it — the host's C compiler will take C too — so
C is the one a group ever names a compiler for.

| | |
| --- | --- |
| suffix | `.c` (and `.h`, which is C by name and still not a source) |
| compiler | `c90` by default; `cl` or the host's `c++` when a group says so |
| targets | `x86_64-windows`, `x86_64-linux`, `arm64-darwin`, `tms6747` |
| debug | `-g -D_DEBUG=1` — DWARF on two targets, CodeView on `x86_64-windows`; the define alone on `tms6747`, which has no line table |
| release | `-O2 -DNDEBUG=1` — c90 optimises the x86-64 targets, and its `-O2` is its `-O1` today |

## Where c90 is found

`--c90`, then `$C90`, then a `c90` beside the editor, then PATH. Naming one
that is not there is worse than naming none — every case that needs it fails
and none of them says why — so the editor drops it with a word and carries on
as if nothing had been named.

## The four targets, and what reaches a program

c90 generates for all four. Of the three host targets only the host's own
reaches a program, because the assembler and linker it hands off to are this
machine's; a cross target stops at the assembly, which the Assembly tab shows.
`tms6747` reaches a program on every machine, through the `asm6x`, `lnk6x`,
RTS6x, `vm6747` and `sim6747` that ship with the editor — [page 6](06-the-project.md).

On Windows a Release build writes **MASM**, assembled and linked by RIDE's own
`masm` and `link` beside the editor; a Debug build writes the GNU spelling,
which clang assembles with CodeView in it and Microsoft's `link.exe /DEBUG`
links — [page 7](07-building.md) says why the two differ. Visual Studio's
`ml64` and `link.exe` reach PATH only after `vcvars64.bat` has run; the editor
arranges that for itself where it needs them, which is why it works from an
ordinary console.

## Debugging

c90 writes **DWARF** for `x86_64-linux` and `arm64-darwin` — line tables,
types, objects and lexical blocks — and gdb and lldb both read it. So a debug
build stops on a line, steps, and shows variables.

On `x86_64-windows` it writes **CodeView** since RIDE 5.1, through the GNU
spelling and clang's assembler, and **cdb** reads it from the `.pdb` that
`link.exe /DEBUG` writes: a Debug build there stops on a line, steps and shows
locals too. The MASM spelling itself carries no line table — which is why
Release and Debug go through different assemblers on that target — and
`tms6747` has no debugger at all; the editor says which case you are in rather
than starting a debugger that cannot work.

**A Mac has one wrinkle.** c90's `arm64-darwin` objects carry no `__eh_frame`
and no `__compact_unwind`, so lldb reconstructs the frame by reading
instructions — and c90 uses the stack pointer as a scratch stack inside a
function body. lldb therefore ends a step early and reports the same line two
or three times. The editor repeats a step until it has been somewhere; see
[page 8](08-debugging.md). `finish` under lldb fails outright on those objects
for the same missing information.

## Sending C somewhere else

A group of C can name `cl` or `c++` instead:

```json
"Legacy": { "files": ["src/old.c"], "toolchain": "c++" }
```

Reasons to: you want an optimiser, or you want debug information on Windows.
Reasons not to: c90 is what this editor exists for, and it is much faster — on
423 files of C it beats `gcc -O0` by a factor of about 57, and produces the
same bytes every time.

`"toolchain": "cl"` compiles `.c` as C++ under `/TP`. That is deliberate and it
is the only way to ask for it; it is not what `auto` will ever do on its own.
