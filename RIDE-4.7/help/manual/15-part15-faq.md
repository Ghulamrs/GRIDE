# Part XV — FAQ: "why does it…"

Short answers to the questions the design provokes, each pointing at the part with
the full story.

**Why are there three compilers instead of one?** Because they are three
languages with genuinely different front ends and, for C vs C++, different rules
about the same syntax. One binary pretending to be all three would have to guess a
language it should be told; instead the suffix (or the Language menu) chooses, and
each compiler refuses a language that is not its own — by name (Parts II, IV).

**Why does the compiler need Visual Studio / vcvars on Windows?** A Release
build needs nothing of Visual Studio's: RIDE's own `masm` and `link` assemble
and link it. A Debug build does — clang's assembler and `link.exe /DEBUG`,
which are what carry CodeView to cdb — and so does a `cl` build or the vendor
fallback; those are on `PATH` only inside a Developer Command Prompt, so the
compiler finds Visual Studio and sources `vcvars64.bat` for them and works from
an ordinary shell too (Part V chapter 22). If yours is somewhere it does not
look, name the file once with *Tools ▸ Locate vcvars64.bat...*.

**Why can't I step through a Release build on Windows?** Its MASM spelling
carries no line table and `ml64` cannot relocate CodeView. A Debug build is
the GNU spelling with CodeView in it, and cdb steps it — C and C++ alike,
since 5.1 (Part V chapter 22).

**Why does `long` change size between platforms?** LLP64 on Windows (and the
C6000) makes `long` 32-bit; Linux/macOS make it 64-bit. The compiler follows each
platform's ABI (Part XI chapter 55). Use fixed-width types when the size matters.

**Why is `volatile` accepted but ignored in cpp11?** It is parsed and dropped; it
does not change codegen. Do not rely on it for memory-mapped I/O (Parts II, X).

**Why does my C++ program using `<thread>` fail at the include?** The shipped
library is a working subset; the large C++11 library components are not there
(Part XIV chapter 72). The failure is at the include, by name, not a miscompile.

**Why won't two Shalimar files link into one program?** Every Shalimar unit
exports the same three startup symbols and the runtime owns `main`, and the
language has no cross-file declarations to check a call against. Shalimar beside C
is two programs (Parts II, III).

**Why does the tms6747 program not produce an `.exe` or `.hex` when I run it?**
Because "run" on tms6747 uses the `vm6747` emulator, which assembles and executes
the `.s` in memory — there is no linked binary. For a real `.out`/`.hex`, use the
TI build path (`bin\ti\ti-build.cmd`), which uses a TI CGT on the machine (Part
VI).

**Is the tms6747 output the same as TI's own compiler's?** No — different
compilers, different codegen; the bytes differ. It is *ABI-compatible* with `cl6x`
(links against TI's objects and runtime, tables match), not byte-identical (Part VI
chapter 25).

**Why does `?` in a converted `printf` add a space?** Shalimar's `?` writes a
space after every item and there is no concatenation or number-to-text builtin, so
a format whose text abuts a hole gains one space. `c2s` carries it with a warning
per `printf` rather than refusing over one space (Part VIII chapter 34).

**Why did the editor open an old program / "do nothing"?** A stale binary. On
Windows, confirm the editor and its compilers are the ones you just built and are
together in `bin\` (Parts I, XIII).

**Where are the binaries? / Where does output go?** The editor and compilers are
in `bin\`; the Shalimar runtime in `bin\lib\`; the C++ headers in `include\`, the
C headers in `lib\`, one level above `bin\`; `-S`/`-c` output lands beside the
source unless `-o` says otherwise; a project's program lands beside its `.pro`
(Part I chapter 6).

**Why is there a "3.0" installer as well as "3.5"?** 3.5 is three languages and
four targets (with the C6000 emulator and the TI build path); 3.0 is the previous
release — three languages, three targets, the frozen original compilers. Both are
provided so a 3.0 project builds against the compilers it was written for.

**Can I use the compilers without the editor?** Yes — that is the point of the
separation. They are ordinary command-line tools; Part IV is the reference and
Part IX the worked examples.

--------------------------------------------------------------------------------
## Index of the parts

- I — the compilation model (input, output, pipeline, native tools, output routing)
- II — the three languages: what each supports and does not
- III — projects: the `.pro`, making and updating, groups, mixed C/C++
- IV — choosing a compiler, the command-line reference, building by hand
- V — the four targets, their native tools, and the ml64/CodeView story
- VI — the C6000 target, the vm6747 emulator, the TI build path
- VII — keys, the editor's CLI, diagnostics, troubleshooting, glossary
- VIII — the Shalimar ecosystem and the c2s converter
- IX — worked examples
- X — C++11 in cpp11, feature by feature
- XI — the ABIs and calling conventions per target
- XII — building from source, the workspace, three-box verification
- XIII — diagnostics, exit codes, and how the tools report
- XIV — the shipped libraries (`lib/` C, `include/` C++)
- XV — FAQ and this index

The full Shalimar language specification is Appendix A
(`help/appendix-a-shalimar-language.md`).
