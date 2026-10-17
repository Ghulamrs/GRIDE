# Locals on the TMS320C6747 - design note

Status, 2026-10-16: phases A, B and C done for scalars and pointers - GRIDEd's Locals pane shows each
function's parameters and locals on the TMS320C6747 for C90, C++11 and Shalimar (tests\gui\run-suite.ps1
checks it). Arrays and structs show as {...} (phase D); TI's own tools see nothing new (phase E).

How the variables travel:

| Step | What happens |
|---|---|
| c90 / cpp11 / shalimar `-g` | write `.dwtag DW_TAG_subprogram, DW_AT_name("f"), DW_AT_low_pc(label)` and a tag per variable |
| asm6x | keeps the tag lines as text in a non-allocated `.gride.dwtag` section of the object |
| lnk6x | carries every command-line object's `.gride.dwtag` into the `.out`, as it carries `.debug_line` |
| sim6747 (`.out`) | reads `.gride.dwtag` from the image (`C6xImage.cpp`) |
| vm6747 / sim6747 (`.s`) | keep the tag lines while assembling (`Asm.cpp`, `Program::dwTags`) |
| both, `Debug.cpp` | `readTags` maps each label to its variables; `info args`, `info locals`, `print x`, `print &(x)` and `whatis x` answer in gdb's shapes. A frame's A15 is the register for frame 0 and, further out, the word the callee's A15 points to (the saved A15). sim6747 flushes in-flight stores before reading memory |
| GRIDEd | reads them as it reads gdb, and asks `whatis` for the Type column on the C6747 |

## The gap

Debugging a C90, C++11 or Shalimar program on the C6747 (sim6747 / vm6747 `--debug`) stops at
breakpoints, steps and returns correctly, but the Locals pane says:

> no locals: the emulator has none - the assembly carries no variable information

GRIDE 2.0 shows the same line, so this is a toolchain gap, not an IDE bug. Windows debugging has
Locals because cdb reads the PDB that clang and link write.

## What the toolchain carries today

| Stage | Line information | Variable information |
|---|---|---|
| c90 / cpp11 / shalimar `-g`, `backend/Tms6747.cpp` | writes TI's `.dwpsn file "f",line n,column c` before each statement | `.dwtag` per parameter and local (below), Debug (-g) only |
| sim6747 / vm6747 | assemble the `.s` themselves (`SIM6747/src/Asm.cpp`) and carry each `.dwpsn` onto the instructions after it; this is how stepping works | none |
| ASM6x / LNK6x | ASM6x builds `.debug_line` from the `.dwpsn` rows (`ASM6x/src/dwarf.cpp`) | `.dwtag` accepted and skipped |
| sim6747 `Session` | keeps its own call stack (a frame per function entry, popped at the return address) | nothing to read |

## Proposal

Use the subset of TI's own DWARF directives that cl6x writes, so the same `.s` stays readable by TI's
tools and by Code Composer Studio:

```
    .dwtag  DW_TAG_subprogram, DW_AT_name("greet")
    .dwtag  DW_TAG_formal_parameter, DW_AT_name("n"), DW_AT_type("int"), DW_AT_location[DW_OP_breg15 -44]
    .dwtag  DW_TAG_variable, DW_AT_name("i"), DW_AT_type("int"), DW_AT_location[DW_OP_breg15 -48]
    .dwtag  DW_TAG_variable, DW_AT_name("sum"), DW_AT_type("int"), DW_AT_location[DW_OP_breg15 -52]
    .dwendtag
```

This is what c90 writes now (`emitVariables` in `Compiler-Ci/src/backend/Tms6747.cpp`). In TI's DWARF
register numbering A0-A15 are 0-15 and B0-B15 are 16-31, so `DW_OP_breg15` is A15, the frame pointer every
c90 function sets to the caller's B15; a local's slot is A15 - 40 - its offset, as `localAddr` reaches it.
A static local is `DW_AT_location[DW_OP_addr symbol]`. One departure from cl6x: `DW_AT_type` names the
C spelling (`"int"`, `"char *"`) instead of a reference to a `DW_TAG_base_type` entry, which phase E has
to turn into real type entries for TI's tools.

How each compiler names a slot:

| Compiler | Where | A variable's location |
|---|---|---|
| c90 | `Compiler-Ci/src/backend/Tms6747.cpp`, `emitVariables` | A15 - 40 - its frame offset |
| cpp11 | `C++Optimize/src/backend/Tms6747.cpp`, `emitVariables` | A15 - saveBytes - its placed offset; left out when -O1/-O2 keeps it in a register or gives it no slot |
| shalimar | `Compiler-Si/src/backend/Tms6747.cpp`, `variableTags` (written at `endFunction`) | A15 - (40 + 8 x slots) + 8k for slot k; `DW_OP_deref` added for a by-reference parameter | Base types first: char, short, int, long, float, double and
pointers. Arrays, structs and Shalimar's `real[][]` follow as `DW_TAG_array_type` /
`DW_TAG_structure_type` in a second step.

### Phases

| Phase | Work | Where | Estimate |
|---|---|---|---|
| A | Each compiler's Tms6747 backend writes `.dwtag` / `.dwattr` for parameters and locals (base types) with `-g` | Compiler-Ci, C++Optimize, Compiler-Si: `src/backend/Tms6747.cpp` | 1-2 days per compiler |
| B | sim6747 and vm6747 parse those directives in `Asm.cpp` and answer the debug session's `locals` command: name, type, value read from A15 + offset of the selected frame (A15 as that frame had it) | SIM6747, VM6747 `src/Asm.cpp`, `src/Debug*.cpp` | 1-2 days |
| C | GRIDEd and GRIDE 2.0: nothing new if B answers in the shape `ReadVariables` already reads for the C6x dialect; otherwise a small parser change | `GdbDebugger.h` / `Debugger.h` | 0.5 day |
| D | Arrays, structs, Shalimar `real[][]` | compilers + simulators | 2-3 days |
| E | ASM6x / LNK6x carry `.debug_info` and `.debug_line` into the `.out` (needed only for debugging a linked image or for CCS) | ASM6x, LNK6x | 3-5 days |

A + B + C give Locals for scalar variables on the C6747 in about a week. D and E can wait.

### How it is checked

`tests\gui\run-suite.ps1` gains a check per language on TMS320C6747: at `main.c:8` Locals shows
`n = 4`; inside `greet` it shows `n`, `i`, `sum`. The same check already passes on Windows.

## Risks

- The three compilers allocate frames differently; each backend has to name the offset it actually uses.
- Optimised (Release) code keeps values in registers: Locals stays a Debug-build feature, as on Windows
  without a PDB.
- Before 2026-10-16 ASM6x, sim6747 and vm6747 rejected `.dwtag`; a toolchain older than that breaks on
  c90's `-g` output on the C6747. Ship the tools together.
- Locals in nested blocks are listed flat for now (c90 tracks the block of each local; phase B can use it
  when two blocks reuse a name).

## After the review (2026-10-16)

Fable's review of the open cases; mended now:

- Static locals under vm6747: `DW_OP_addr L.s.1` resolves through every symbol the assembly defines, not only
  function labels (`DebugVm.cpp` addressOfFunction).
- A name an inner block reuses: both variables listed, each with its own value; `print`, Watch and Set Variable
  take the last-declared one, the inner block's (`Debug.cpp` cmdInfoVariables, variableNamed).
- `p.x`, `a[i]`, `ptr->m`, `*ptr` and chains of them in print, whatis, `print &`, Watch, Set Variable and `x`
  (`Debug.cpp` lvalueOf).

Deferred:

| Phase | Work | Why later |
|---|---|---|
| E | real DWARF `.debug_info` in the `.out`, `DW_AT_type` as DIE references | only TI's own tools / CCS would see it; GRIDE reads `.gride.dwtag` |
| G | cpp11 -O1/-O2: a variable held in a register as `DW_OP_regN` (frame 0 only) | -O0 is the debug configuration; gdb shows these as `<optimized out>` too; cpp11 refuses -g with -O on the C6747 anyway |

## Phase F - done (2026-10-17)

c90 and cpp11 (`Tms6747.cpp` emitVariables / emitScope) write each inner block that declares something as
`.dwtag DW_TAG_lexical_block, DW_AT_low_pc(L$fn$blk$bN), DW_AT_high_pc(L$fn$blk$eN)` ... `.dwendtag`, nested
as the blocks are, from the Walker's block labels (`resetBlocks` before the body; cpp11 sets the caller's
blocks aside while an inlined callee is walked). A block with no code of its own is folded into its parent.
sim6747 and vm6747 (`Debug.cpp` readTags) give each variable its innermost block's labels, the machine
places them (`placeBlocks`), and `info locals`, `print`, Watch and Set Variable take only the variables whose
block holds the selected frame's pc (an outer frame's: its return address less one). A label that does not
resolve leaves the variable listed for the whole function, as before. Shalimar has no nested blocks of locals.
