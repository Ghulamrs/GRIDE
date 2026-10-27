# masm tests: GRIDE's masm against Microsoft's ml64

Run `python check.py` here. It assembles every `.asm` in this folder with GRIDE's masm
(`GRIDEd\x64\Release\masm.exe`) and with ml64, and fails unless every section's bytes, every relocation and
the external symbols are the same. `--masm` and `--ml64` (or the `ML64` environment variable) name other
executables; ml64 is used for checking only.

- `instructions.asm` - every form ml64 takes of the instructions GRIDE's Windows runtime needs: the SSE2
  integer ops, SSE4.1's rounding, MOVD/MOVQ, the non-temporal stores and the prefetches, MXCSR and the x87
  control word, the bit scans and counts, the string ops with REP; register and memory operands, xmm0-15
  and r8-r15, every address mode, RIP-relative and absolute-indexed operands on extern and local data.
  `python gen_instructions.py` writes it.
- `nokeyword.asm` - `OPTION NOKEYWORD:<names>`: functions and data named fabs, div, lea, eax, xmm7,
  offset, near, title, segment..., defined, public, extern and referenced; the lists spelt every way ml64
  takes them.
