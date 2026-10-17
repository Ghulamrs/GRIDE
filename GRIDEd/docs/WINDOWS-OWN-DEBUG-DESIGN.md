# Windows debugging and runtime of our own (W1, W2) - design note, not adopted

## Decision (the owner, 2026-10-17)

**Not adopted, for the time being.** The owner disagrees with this idea for now:

> No product closes in the time we predict, because perfection has no end. Seeing cpp11's completion
> curve, we could be trapped in an exercise that never finishes. So we believe in Windows and Visual
> Studio being present, and do not implement so many components that their maintainability becomes
> an issue in itself.

So GRIDE keeps relying on Windows and Visual Studio for Windows programs: Visual Studio's C runtime and
the Windows SDK to link, clang and Microsoft's link.exe for a Debug build's CodeView and PDB, and cdb to
debug. Nothing below is scheduled. The note keeps the analysis so the question need not be worked out
again if it is ever reopened.

## What Windows depends on today

| Step | What does it | Whose |
|---|---|---|
| Compile | c90, cpp11, shalimar | ours |
| Assemble and link, Release | masm, link | ours |
| C runtime in every Windows program | `libcmt` / `libucrt` / `libvcruntime` (Visual Studio), `kernel32.lib` (Windows SDK), `legacy_stdio_definitions.lib` - the `hostCrt` entry of toolchain.json; link finds them through LIB as `vcvars64.bat` sets it | Microsoft |
| Debug build | `-masm=gnu`, then clang (Visual Studio's LLVM) for CodeView, then Microsoft's link.exe `/DEBUG` for the PDB. In the MASM spelling c90 refuses `-g`: MASM carries no line table and a Windows debugger wants CodeView | Microsoft |
| Debugger | cdb reading the PDB (Windows SDK's Debugging Tools) | Microsoft |
| Visual Studio projects (cppparse) | cl and link.exe | Microsoft, by their nature |

Consequences accepted with the decision:

- A machine without Visual Studio (or its Build Tools) and the Windows SDK cannot link a Windows program,
  Release included, nor debug one. The installer's Setup does not bring them.
- Updates to Visual Studio, the SDK or cdb can change what GRIDE finds or reads. toolchain.json already
  absorbs most of it (vswhere, the newest MSVC folder or `msvcVersion`, the SDK found through the registry,
  cdb found under Windows Kits by `lookup.dirs`); cdb's output is the part that has needed code fixes
  (ILT thunks, steps into code with no source, program output read as a stop - October 2026).

## The idea, as proposed

Make the Windows side what the TMS320C6747 side already is - every link ours:

    compiler -g  ->  .dwpsn / .dwtag  ->  asm6x keeps them  ->  lnk6x carries them  ->  vm6747 / sim6747 Debug.cpp
                                                                                       answers GRIDE in gdb's words

### W1 - a Windows debugger of our own

- The compilers' x86_64-windows back ends write the `.dwpsn` line rows and `.dwtag` variable tags they
  already write for the C6747 (the Walker's block tracking is shared - phase F).
- masm keeps them in an object section of our own; link carries the section into the .exe, as asm6x and
  lnk6x carry `.gride.dwtag` and `.debug_line`. No CodeView, no PDB.
- A small debugger, dbg64, runs the program under the Windows debugging API (a process created under debug,
  its debug events, `int3` breakpoints, the trap flag for a single step, thread context for registers,
  process memory for reads and writes) - an API unchanged since Windows NT. Its command shell is the same
  `Debug.cpp` vm6747 and sim6747 use, behind a new `Machine`; GRIDE talks to it as it talks to vm6747
  (Locals, Watch, Memory, the call stack, lexical blocks).
- Removes: cdb, the PDB, clang and Microsoft's `link /DEBUG` from the Debug build.
- Estimated about 1-2 weeks, in steps like the C6747 Locals phases A-F.

### W2 - a Windows runtime without Visual Studio

- Windows 10 and 11 carry `ucrtbase.dll`; link would generate import libraries for it and for
  `kernel32.dll` itself, with a small start-up routine of our own, in place of `libcmt` / `libvcruntime`
  and the SDK's `kernel32.lib`.
- A Windows program would then build and debug on a bare Windows machine with GRIDE's Setup alone.
- Estimated about 1 week.

### What would have stayed Microsoft's either way

Windows itself - the kernel32 API, the loader, `ucrtbase.dll` - and Visual Studio's own projects
(cppparse), which keep cl and cdb.

### What W1 would really touch (found in the sources, 2026-10-17)

The estimate above was made before the sources were read; they show more work, not less:

- All three compilers refuse `-g` in the MASM spelling (c90 `Driver.cpp:830-839`, cpp11 `Driver.cpp:1054-1063`,
  shalimar switches to clang's COFF emitter): each driver and back end needs a new debug path.
- cpp11's Windows frame is `push rbp; sub rsp,N; mov rbp,rsp` - rbp is set after the allocation, so the saved
  rbp sits at `[rbp+frameSize]`, not `[rbp]`; c90 and shalimar keep the classic chain. cpp11 `-O` also keeps
  locals in registers and inlines.
- Our masm has no discardable or info section and no SECREL relocation; a tag section would be loaded into the
  image, and link truncates a PE section name to 8 characters and strips what follows `$`.
- link writes no import library (no `/def`, `/implib`) and drops `.debug$*`; it only reads Microsoft's.
- `Debug.cpp` / `Debug.h` are 32-bit throughout (pc, registers, addresses, pointer reads, `x/`) and keep the call
  stack from B3 and A15: a 64-bit Windows `Machine` changes the shared interface, not just adds a class.
- GRIDEd hard-codes cdb for a host target (`GdbDebugger.h:899`), asks `whatis` only for the C6x dialect
  (`:1104`), and routes every `debugInfo: "codeview"` Debug build to clang and Microsoft's link.
- For W2: the Shalimar Windows runtime is itself built with cl `/MT` and uses the C runtime and the C++ library
  (`std::string`, `std::vector`), so it would need rewriting too.

## Why it is set aside

- **Components to maintain.** W1 adds a debugger and debug-information paths in three compilers, masm and
  link; W2 adds import-library generation and a start-up routine. Each is one more thing to keep working as
  the compilers grow - the maintenance the decision avoids.
- **The finishing curve.** cpp11's own completion has shown how such work extends; the estimates above would
  be at the same risk.
- **What is already there works.** Windows debugging through cdb passes GRIDEd's suite (80 checks, Windows
  and the C6747) and the showcase examples; Visual Studio is present wherever GRIDE is developed and used.

## If it is reopened

Start from W1 alone, with the narrowest first step: line stepping only (`.dwpsn` through masm and link,
dbg64 with breakpoints and steps), checked against the Windows half of `tests\gui\run-suite.ps1`, before any
Locals work - and only once cpp11's Compiler-Cpp-Optimize phases are finished, so the toolchain under it is
settled.
