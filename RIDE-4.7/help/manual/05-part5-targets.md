# Part V — The four targets, their native tools, and how Windows is debugged

A *target* is the machine the code is generated for. It is a separate thing from
the *host* — the machine you are compiling on. The compiler can generate any
target's assembly on any host; whether that assembly can be turned into a program
depends on whether the host has the assembler and linker for it. This part is the
four targets, what each needs, and the Windows tool story in detail.

--------------------------------------------------------------------------------
## 19. Host versus target, and "runs here"

The compiler's own output is assembly (Part I). Turning assembly into a program
needs a native assembler and linker *for that target*, and those live on the
host. So:

- Generating a target's assembly (`-S`) works on **any** host.
- Building and running a target's program works only where the host has that
  target's tools.

On each host, one target is native and the others reach `-S` and stop:

| Host             | Native target     | The others reach       |
|------------------|-------------------|------------------------|
| Windows          | `x86_64-windows`  | `-S` only              |
| Linux            | `x86_64-linux`    | `-S` only              |
| macOS (arm64)    | `arm64-darwin`    | `-S` only              |
| any              | `tms6747`         | assembled by `asm6x`, linked by `lnk6x`; runs on `vm6747` and `sim6747` |

`tms6747` is the exception: its assembler, linker, runtime, emulator and
simulator ship with the editor and are host-independent, so the fourth target
builds and runs on all three hosts. When the editor says a target "only reaches
-S here — switch to <host> to run it", that is this fact, not a failure.

--------------------------------------------------------------------------------
## 20. The three host targets

**`x86_64-linux`** — 64-bit x86, System V ABI, GNU assembler and linker. Debug
info is DWARF. This and `x86_64-windows` share **one instruction stream**: the
same code generator produces both, and the only difference is how the
instructions are spelled — GNU syntax here, MASM there. That is why `-masm=gnu`
on `x86_64-windows` produces the same program: it is the same instructions in the
other dialect.

**`arm64-darwin`** — 64-bit ARM, the Apple ABI, the assembler and linker `clang`
drives, DWARF (gathered into a `.dSYM` on a real link). This is the native target
on an Apple-silicon Mac.

**`x86_64-windows`** — 64-bit x86, the Microsoft ABI, two spellings of one
instruction stream: **MASM**, which RIDE's own `masm` and `link` assemble and
link for a Release build, and the **GNU spelling**, which clang's assembler and
Microsoft's `link.exe /DEBUG` turn into a program with **CodeView** in it for a
Debug build. The MASM spelling carries no line table; the GNU one carries
CodeView since 5.1 — see the next chapter. This is the native target on the
Windows box, and the one the installers target.

The type model is shared where the standard allows and differs where the ABI
demands: `long` is 64-bit on the Unix targets and 32-bit on Windows (LLP64),
which the code generator knows per target.

--------------------------------------------------------------------------------
## 21. `tms6747` — the fourth target

The TI **TMS320C6747** (a C674x, C6000-family VLIW DSP). The compiler generates
C6000 assembly; RIDE's own **`asm6x`** assembles it, **`lnk6x`** links the
objects against **RTS6x**, RIDE's C6747 runtime, into a `.out`, and two
programs run the result on any host — the **`vm6747` emulator**, which takes
the assembly text as it is, and the **`sim6747` simulator**, which runs the
linked `.out`. A `.out` for the chip can also be made through TI's own tools
(Part VI). The C6000 has its
own ABI (arguments and small-struct returns in A/B register pairs, DP-relative
near data, TI's exception-table format), which the backend implements to match
TI's `cl6x` — verified word-for-word. wchar_t is 16-bit and unsigned on the
C6000; `long` is 32-bit; `long double` is 8 bytes. Part VI is this target in
full.

--------------------------------------------------------------------------------
## 22. The two Windows spellings, and how a Windows program is debugged

This is the most-asked Windows question, so here it is in full.

**The MASM spelling, and the project's own `masm`.** On `x86_64-windows` the
compilers can write MASM-syntax assembly — the same instruction stream as the
Linux target, spelled Microsoft's way (`MasmSpelling` versus `GnuSpelling`;
the two are the whole of the difference). Where a construct has no MASM room —
for instance an identifier that MASM treats as a reserved word — the backend
spells around it (e.g. `OPTION NOKEYWORD` for a name like `fabs`, `OPTION
PROC:PRIVATE` so a `static` function is not exported as an external symbol).
These are decisions recorded at the emission site, not accidents. Since 4.0
the assembler that reads it in a RIDE build is RIDE's own **`masm`** beside the
editor, held to Microsoft's `ml64` byte for byte on the compilers' corpus, and
the linker RIDE's own **`link`**; Microsoft's `ml64.exe` and `link.exe` enter a
build only when one of ours has failed and you say yes to the editor's
question (Part I chapter 5). RIDE itself never names `ml64`.

**Where Visual Studio's tools must be *found* first.** `ml64`, `link` and `cl`
are on `PATH` only inside a Visual Studio Developer Command Prompt. An editor
started from Explorer is not one. So where a build needs one of them — a
Debug build's `link.exe`, a `cl` build, the vendor fallback — the compiler and
the editor locate Visual Studio themselves and run the step inside a shell
that has sourced `vcvars64.bat`. That sets both `PATH` and `LIB` (so the
linker finds `libcmt.lib`). The
search, in the compilers and the editor alike: already inside a developer
prompt (`VCToolsInstallDir` set), nothing to do; else `vswhere` asked for the
newest installation with the C++ tools, any version or edition, Build Tools
and previews included; else the places the installer puts them - `Microsoft
Visual Studio\18`, `\2022`, `\2019`, `\2017`, each edition. When none of
that finds it - a Visual Studio somewhere of its own - *Tools ▸ Locate
vcvars64.bat...* names the file once, in the installation's `settings.json`
(`"vcvars"`), and every build from then on uses it. Before this, a build
started from a double-clicked editor died with `'ml64.exe' is not
recognized`, which read like a broken compiler and was a missing
environment. The lesson: on Windows, "the compiler works from a developer
prompt but not from the editor" is always an environment question, never a
code-generation one.

**Why a Debug build does not go through the MASM spelling.** A native Windows
debugger wants **CodeView**, not DWARF, and two things keep the MASM path from
carrying it:

1. **MASM carries no line table of its own**, and neither `ml64` nor `masm`
   builds one from the assembly. So `-g` for `x86_64-windows` in the MASM
   spelling has nowhere to put a line table — the compiler refuses `-g` there
   and says why, rather than emit debug info a debugger cannot use.
2. **`ml64` cannot relocate CodeView.** Emitting CodeView records into the
   assembly and having `ml64` fix them up was tried and does not work — `ml64`
   will not relocate those sections.

**How a Debug build is debugged, since 5.1.** The GNU spelling can. With
`-masm=gnu -g` on `x86_64-windows`, c90 and cpp11 write CodeView — the line
table, the procedure records and the locals — into the GNU-spelled assembly,
**clang's assembler** (Visual Studio ships one) turns it into a COFF object
with `.debug$S` in it, and **Microsoft's `link.exe /DEBUG`** writes the `.pdb`
that **cdb** reads. The editor gives every Debug build on this target exactly
that: `-masm=gnu`, clang, `link.exe /DEBUG`, cdb — and a Release build the
MASM spelling through RIDE's own `masm` and `link`. One target, two
assemblers, and the configuration says which; the editor names what ran in
the console.

**C++ on Windows through `cl`** is debugged the same way it always was: `cl`
writes CodeView into a `.pdb` and `cdb` reads it (cdb is installed under the
Windows Kits, found by the debugger even though it is not on `PATH`). So on
the box every Debug build of C or C++ steps, whichever compiler made it; what
cannot be stepped is a Release build, and a tms6747 program, which has no
debugger. The editor's status and Debug menu say which case you are in rather
than failing quietly.

**Summary of the Windows debug matrix:**

| Built by | Target          | Debug info | Steppable on the box |
|----------|-----------------|------------|----------------------|
| `c90`, `cpp11` | x86_64-windows, Debug (GNU spelling, clang, `link.exe /DEBUG`) | CodeView | yes (cdb) |
| `c90`, `cpp11` | x86_64-windows, Release (MASM, `masm`, `link`) | none | no (MASM, no line table) |
| `cl`     | x86_64-windows  | CodeView   | yes (cdb)            |
| `c90`/`cpp11` | x86_64-linux / arm64-darwin | DWARF | yes (gdb/lldb) |
| any      | tms6747         | none       | no (runs on the emulator or the simulator; neither is a debugger) |
