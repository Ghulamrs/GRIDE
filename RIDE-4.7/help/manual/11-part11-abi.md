# Part XI — The ABIs: how each target passes arguments and lays out types

An ABI is the contract between compiled pieces: which registers carry arguments,
how a result comes back, how wide each type is, how names are decorated. RIDE's
compilers implement each target's real ABI so their output links against the
platform's own tools and libraries. This part is that contract, target by target
— useful when a program crosses a boundary (a call into a system library, or a
mixed link) and you need to know exactly what travels where.

--------------------------------------------------------------------------------
## 54. Why the ABI is per-target and the instructions are (mostly) shared

The backends share one statement walk and differ where the target genuinely does
(Part I chapter 4). Expressions, and **calls most of all**, are per-target,
because that is where ABIs part. Two consequences follow:

- The two x86-64 targets share **one instruction stream**; only the spelling
  differs (GNU vs MASM). So `x86_64-linux` and `x86_64-windows` compute
  identically and differ in argument registers and type widths, not in the code
  that does the arithmetic.
- `arm64-darwin` and `tms6747` each have their own instruction set and their own
  ABI, so both the code and the contract differ.

A difference between targets therefore lives in a small, visible place — the call
lowering and the `Target`/`Abi`/`Spelling` triple — not smeared through the
generator.

--------------------------------------------------------------------------------
## 55. Type widths across the four targets

| Type          | x86_64-linux | x86_64-windows | arm64-darwin | tms6747 |
|---------------|--------------|----------------|--------------|---------|
| `char`        | 1 (signed)   | 1 (signed)     | 1 (signed)   | 1       |
| `short`       | 2            | 2              | 2            | 2       |
| `int`         | 4            | 4              | 4            | 4       |
| `long`        | **8**        | **4** (LLP64)  | 8            | **4**   |
| `long long`   | 8            | 8              | 8            | 8       |
| pointer       | 8            | 8              | 8            | 4       |
| `wchar_t`     | 4            | 2              | 4            | **2, unsigned** |
| `long double` | 16/8*        | 8              | 8            | 8       |
| `bool`        | 1            | 1              | 1            | 1       |
| `size_t`      | 8            | 8              | 8            | 4 (`unsigned int`) |

The two that catch people: **`long` is 32-bit on Windows** (LLP64) and on the
C6000, but 64-bit on Linux/macOS; and **`wchar_t` is 16-bit** on Windows and the
C6000. `wchar_t` is always mangled `w` regardless of its width — the width and the
mangling are separate facts. (*`long double` breadth on Linux depends on the
build; the compiler follows the platform.)

--------------------------------------------------------------------------------
## 56. x86-64 — System V (Linux) and Microsoft (Windows)

**x86_64-linux (System V AMD64).** Integer/pointer arguments in
`rdi, rsi, rdx, rcx, r8, r9`, then the stack; integer results in `rax` (`rdx:rax`
for a 128-bit pair). Floating arguments in `xmm0..7`. Callee-saved:
`rbx, rbp, r12–r15`. Assembled by the GNU assembler, linked by the host driver;
DWARF debug info.

**x86_64-windows (Microsoft x64).** Integer/pointer arguments in
`rcx, rdx, r8, r9`, then the stack, with a 32-byte **shadow space** the caller
reserves; results in `rax`. Floating arguments in `xmm0..3`. Callee-saved
includes `rsi, rdi` (unlike System V). `long` is 32-bit (LLP64). Two
spellings: MASM for RIDE's `masm` and `link` in a Release build, with **no
line table**, and the GNU spelling for clang's assembler and `link.exe /DEBUG`
in a Debug build, with CodeView (Part V chapter 22). A `static` function is
kept out of the object's external symbols
with `OPTION PROC:PRIVATE`, and a name colliding with a MASM reserved word is
handled with `OPTION NOKEYWORD` — decisions made at the emission site.

The same instruction stream underlies both; the argument registers, the shadow
space, the callee-saved set and `long`'s width are the whole of the difference.

--------------------------------------------------------------------------------
## 57. arm64-darwin (AArch64, Apple)

Integer/pointer arguments in `x0..x7`, results in `x0` (`x1:x0` for a pair);
floating in `v0..v7`. Callee-saved `x19..x28`, the frame in `x29`/`x30`. The Apple
variations from the generic AArch64 ABI (argument passing on the stack, `char`
signedness) are followed. Assembled and linked through `clang`; DWARF gathered
into a `.dSYM` on a real link (the driver runs `dsymutil` when it compiled the
sources itself; a link of objects alone has the debug map asked for before the
objects are removed). This is the native target on an Apple-silicon Mac.

--------------------------------------------------------------------------------
## 58. tms6747 — the C6000 EABI

The C674x is a VLIW DSP with two register files (A and B). The compiler implements
TI's EABI, matched to `cl6x` and verified word-for-word:

- **Arguments** in `A4, B4, A6, B6, A8, B8, A10, B10, A12, B12`, then the stack
  from `B15 + 4`; a 64-bit value in an even:odd pair.
- **Small-struct return** (≤ 8 bytes) in `A5:A4`; a larger struct through a hidden
  pointer in **A4, as the first parameter, before `this`**. A small struct is
  also **passed** by value in a register or pair.
- **Near data**: scalars reached DP-relative through `B14`, in TI's near sections.
- **Callee-saved**: `A10/B10/A12/B12` (and the frame registers) are preserved by a
  function that uses them across a call — which `shalimar` also now does, so a
  TI-compiled caller of a Shalimar comparator is safe.
- **Exceptions**: TI's tables — `.c6xabi.extab`/`-style scope descriptors, an
  index entry per function, `__cxa_end_cleanup` at a cleanup pad — so a C++
  exception thrown through our frames behaves as TI's runtime expects.
- **C++ layout** (vtables, VTT, typeinfo, virtual thunks) matches `cl6x`'s tables
  exactly, so our objects interoperate with TI-built ones.
- **Assembly** in TI's syntax: `$` for the dots in names, `.ref` for undefined
  symbols, `EXTU`/`CLR` where an `AND` immediate has no room, `||` parallelism and
  unit specifiers.

Because the ABI matches `cl6x`, our `.s` assembles under `cl6x`/`asm6x` and links
under `lnk6x` against TI's runtime — which is what the TI build path (Part VI)
relies on. It is ABI compatibility, not byte-identical code (Part VI chapter 25).

--------------------------------------------------------------------------------
## 59. Name decoration (mangling)

- **C** names are undecorated (a leading underscore on some platforms, per the
  platform convention).
- **C++** names use the **Itanium** scheme on the three Itanium targets
  (`_Z...`), and the **Microsoft** scheme on `x86_64-windows` (`?...`), so a
  symbol table dumped from our object matches what the platform's linker expects
  — this is checked against `cl` on Windows with a symbol-table oracle, not only
  against clang's emulation.
- **Shalimar** exports the same three startup symbols from every unit, with the
  runtime owning `main` — which is why two Shalimar objects do not link together
  (Part II chapter 9).

When a program crosses a boundary, the mangling is what lets the linker match the
call to the definition; it is the visible half of the ABI, and it is verified
against the platform's own tools rather than assumed.
