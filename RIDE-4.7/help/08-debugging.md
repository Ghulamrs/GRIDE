# 8. Debugging

## The keys

| | |
| --- | --- |
| `F9` | set or clear a breakpoint on this line |
| `F8` | start, or carry on from where it stopped |
| `F7` | step over |
| `F6` | step into |
| `Debug ▸ Step out` | run until this call returns |
| `Ctrl-Up` / `Ctrl-Down` | up the call stack, and back down |
| `Debug ▸ Watch expression...` | keep asking about an expression |
| `Debug ▸ Stop debugging` | end the session |

**`F8` debugs the file in front of you; `Debug ▸ Debug project` debugs the
program the project builds.** The same two things `Ctrl-B` and `F4` choose
between, asked the same way.

A breakpoint is the editor's own note and needs no compiler: you can set one,
see it in the gutter, and take it away with nothing installed at all. It is
filed under the file rather than the buffer, so it survives the file being
closed and opened, and it follows the file when you rename it.

## Where it stopped

The gutter marks the line with `>`; a breakpoint is `*`, and the arrow wins
when both are on the same line. **The file it stopped in is opened if it was
not already** — stepping out of one file of a project into another used to say
"stopped at main.c:13" while showing `circle.c`, which is a stranger lie than
the one being avoided.

The Debug tab shows the line, the function, what is in scope, and who is
waiting for it. `Ctrl-W` twice puts the cursor in the panel; enter on a
variable sets it, enter on a frame goes to that frame, and enter on the top
line comes back to the stop.

## What can be debugged, and what cannot

| | |
| --- | --- |
| c90 or cpp11 on `arm64-darwin`, `x86_64-linux` | lldb or gdb, reading the compiler's own DWARF |
| c90 or cpp11 on `x86_64-windows` | cdb, reading the CodeView a Debug build carries — since 5.1; a Release build is MASM, which has no line table, and cannot be debugged |
| c90 or cpp11 on `tms6747` | **no** — no line table, and the emulator and the simulator are not debuggers |
| `cl` | cdb, reading CodeView from the `.pdb` |
| `clang++`, `g++` | lldb or gdb |
| `shalimar` on `arm64-darwin`, `x86_64-linux` | lldb or gdb, reading the DWARF a Debug build carries — since Compiler-Si's rungs D1 to D3 (2026-10-13): a breakpoint on a line, stepping, the stack, the parameters, locals and globals by name, an array as the runtime's `{count, element, data}`, and the blocks a name hides inside |
| `shalimar` on `x86_64-windows` | cdb, reading the CodeView a Debug build carries — since rung D4 (2026-10-13), by the road c90 and cpp11 took: clang's assembler and `link.exe /DEBUG` |
| `shalimar` on `tms6747` | `vm6747 --debug` on the assembly, or `sim6747 --run --debug` on the image — since rung D5 (2026-10-13): a breakpoint on a line, stepping and the stack, no variables, as for cpp11 there |

**Debug information does not mix.** A program linked from two compilers has a
debugger that can see part of it. The editor starts the first debugger any part
has and names the groups it will not be able to stop in, in the console, before
the build starts — rather than letting you find out by pressing `F8`.

**Release cannot be debugged**, and the message says the true reason for the
compiler you are using: `-g` for c90, and for shalimar that release links a runtime
with no debugger in it.

**On Windows, a Debug build of c90 or cpp11 goes through clang's assembler and
Microsoft's `link.exe /DEBUG`**, not through RIDE's own `masm` and `link`: the
MASM spelling carries no CodeView and LINK writes no `.pdb`, so a program the
project's own tools built could not be stopped on a line. [Page 7](07-building.md)
has the two configurations side by side. What cdb then gives is what it gives
a `cl` build - a breakpoint on a line, stepping, locals and the stack - with
one difference: the editor keeps no position for a closing brace, so a step
does not stop on `}` where cl's build would.

## Shalimar is different, and it is not a lesser version

**On a Mac and on Linux that is no longer the whole story.** Since Compiler-Si's
rungs D1 to D3 (2026-10-13) `shalimar -g` writes DWARF for `arm64-darwin` and
`x86_64-linux` — the line table, each function's frame, its parameters, locals
and the globals, and the lexical blocks — and a Debug build asks for it, so there
lldb or gdb drives a Shalimar program exactly as it drives a c90 one: `F9`, `F8`,
`F7`, `F6`, the stack, the variables, and a watch. An array shows as what it is
at run time, the runtime's `{count, element, data}`: `a->count` is its capacity
and `a->data[i]` an element. On Windows a Debug build of shalimar goes the way a
c90 one does - clang's assembler and `link.exe /DEBUG`, cdb reading the `.pdb` -
since rung D4. And on `tms6747` a Debug build writes cl6x's `.dwpsn` before each
statement, as cpp11's does, so `vm6747 --debug` stops it on a line, steps it and
walks its stack - lines and frames and no variables, since the C6747 path carries
no types or locations (rung D5). The session below is what the program keeps for
anyone without those tools.

A Shalimar program **stops itself**. The compiler already emits
`shm_line(unit, line)` before every statement so a runtime error can name where
it happened, and a debug build offers that same position to a session inside
the program. There is no debug format, nothing to install, and no gdb, lldb or
cdb involved.

What that buys: statement granularity rather than an approximation, and the
same behaviour on all four targets — including `tms6747`, where nothing else can stop: a Debug build
there runs its `.out` on `sim6747`, and the program's session talks to the
editor through the simulator's host channel, as it does through a pipe on a host.

What it cannot do is **read a variable**. The compiler emits no table of a
function's names against its frame slots, so the Debug tab says
*"a Shalimar program says where it is, not what is in it"* rather than showing
an empty list. Watches and walking the stack refuse in the same voice, and the
tab does not offer keys for them.

## A step that appears to do nothing

On a Mac, stepping used to need three presses of `F7` where Linux needed one.
The reason is underneath both: c90's `arm64-darwin` objects carry no
`__eh_frame` and no `__compact_unwind`, so lldb works the frame out by reading
instructions — and c90 uses the stack pointer as a scratch stack inside the
body, so the frame it computes moves mid-function and lldb ends the step early.

The editor now repeats a step until it has actually been somewhere. The test is
narrow on purpose: same file, same line, same function, with the address
further on. Recursion on one line is on that same line too, but its address
goes *back* to the callee's prologue, so it reads as the arrival it is.
