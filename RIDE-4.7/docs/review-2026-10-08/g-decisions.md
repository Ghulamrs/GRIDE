# WS-G decisions - for WS-B's amendments

Written first, 2026-10-08, on branch `review/g-ride` off RIDE main `1faca4c`.
Workstream G of `C++Optimize/docs/REVIEW-PLAN-2026-10-08.md`. Each decision
below is binding on the code this branch lands; WS-B's step 9 amends the
passages its note (section 6) lists from these words, and this branch does
that amendment itself in its own worktree, as instructed.

## R5 - Run (F5) on tms6747 is the linked `.out` on sim6747 (the user's decision of 08-10)

- **Run file (F5) and Run project** on `tms6747` build the TI program -
  cpp11/c90 `-S`, asm6x, lnk6x against RTS6x - and run `<program>.out` on
  **sim6747**. So asm6x, lnk6x and RTS6x are on every run's path. A single
  file's F5 links its `.out` beside its `.s` for this, as Run on Simulator
  already did (`Toolchain::linkSingleFile`).
- **Build ▸ Emulate on vm6747** is the new, explicit item - the old F5: the
  compiler's `.s` (or `<program>.vm`) handed to vm6747, which reads the
  assembly text and runs it over a C library and an EH runtime of its own. It
  takes the place "Run on Simulator" had in all three Build menus (console
  `menu.cpp`, WinForms, macOS), since Run on Simulator is now what Run does.
  **Verify** stays: both runs, compared.
- The console editor: `--run` and `--simulate` are the same thing for tms6747
  (`--simulate` is kept as a synonym so scripts and the suite keep working);
  **`--emulate`** is the new flag, the old `--run`. The bridge gains
  `RIDE_RUN_EMULATOR = 3`; `RIDE_RUN_SIMULATOR` stays and means what
  `RIDE_RUN_PROGRAM` means for a tms6747 program.
- The console line before the run names the runner: `$ sim6747 --run
  <program>.out` for a Run, `$ vm6747 <program>.vm` (or `.s`) for an Emulate.
- sim6747's `--run` takes no program command line; a project with Build ▸
  Command-line arguments is told `[run] the simulator takes no command line -
  Emulate on vm6747 hands them over` and runs without them, as Verify already
  says.
- A host target is unchanged: Run runs the native program; Emulate on vm6747
  on a host target answers "Emulate on vm6747 is for tms6747 - choose it under
  Target", as Run on Simulator did.

## D2 wording - a tms6747 build without a `.out` is a failed build

`makeTiProgram` returns `result.ok = false` and says why, in each of the
states that used to return success silently:

| state | line |
| --- | --- |
| no asm6x beside the editor (and none in `$ASM6X`) | `no asm6x beside RIDE: a tms6747 build is a TI program, and nothing here assembles one - Build > Emulate on vm6747 runs the .s without it, and "emulateOnly": true in the project's .pro makes that the project's build` |
| `$ASM6X` names a file that is not there | `asm6x named by $ASM6X is not there: <path>` |
| asm6x ran; neither RTS6x in `lib/rts6x-tms6747` nor TI's compiler named under Tools | `[N TI objects made] no .out: a tms6747 build links against RTS6x in lib/rts6x-tms6747 beside RIDE, or TI's runtime when its compiler directory is named under Tools, and neither is here` |
| TI's directory found (not named) without `rts6740_elf_eh.lib` | `[N TI objects made] no .out: <ti> has only rts6740_elf.lib - these objects need rts6740_elf_eh.lib, named under Tools (TI library directory)` |
| no `.s` to assemble | unchanged (nothing to make) |

`"emulateOnly": true` in the `.pro` (`Project::emulateOnly()`, carried on
`Toolchain::emulateOnly`) is the one way to build without a `.out`: the build
then ends `[no .out: the project says emulateOnly - Run is Emulate on vm6747]`
and succeeds, and Run (F5) on such a project emulates. A single file has no
`.pro`: its Run needs the tools, its Emulate does not (an Emulate does not
link). The refusals asm6x and lnk6x already made are unchanged.

`tests/test.cpp` gains the three states (no asm6x; asm6x but no runtime;
emulateOnly) and the fourth (a named asm6x that is gone).

## D5 wording - the prompted native retry marks what it made

- The console line on a Yes: `building again with <theirs> in place of the
  project's <ours>, as asked` - e.g. `building again with Visual Studio's ml64
  and link.exe in place of the project's masm and link, as asked`, or `...
  TI's lnk6x in place of the project's lnk6x ...`.
- The build log and the Output window end, when the retry built the program:
  `[built with Visual Studio's ml64 and link.exe, not RIDE's masm and link]`
  (`[built with TI's lnk6x, not RIDE's lnk6x]` for tms6747). When the retry
  failed too: `[not built: <theirs> failed as well]`.
- The two names are the ones the question itself used, so they cannot
  disagree with it.

## P3 recipe - a tms6747 group's sources compile at once

The plan's recipe was one `cpp11 -S -arch tms6747 <sources> -o <dir>`. Both
drivers refuse it as they stand: `-S` with several inputs writes each `.s`
beside its source and `-o` with several inputs is an error (`cpp11 Driver.cpp:
"-o names a single output, but N inputs were given"`, c90 the same), and the
Driver is WS-F's file. So the recipe is **RIDE's own pool**: `objectRecipe`
and `targetRecipe` hand back the per-source `-S` commands as a list
(`Recipe::commands`, the joined `&&` form kept in `command` for the line the
console shows), and `compile.cpp` runs them on `min(sources, cores)` threads,
each command's output captured whole and written to the console in source
order as it completes. The per-file `.s` names are unchanged. When a driver
gains `-S -o <dir>`, `commands` becomes one entry and nothing else moves.

## D14 - `--require-tools`

`tests/test --require-tools` turns a skip into a failure, and names the skip.
A skip is one of RIDE's own tools or inputs missing (a compiler, asm6x, lnk6x,
vm6747, sim6747, RTS6x's libraries, RIDE.exe, `docs/ccs-reference`, the host's
debugger). A third party's input - TI's `rts6740_elf_eh.lib` for the CCS
reference projects - is said as `external` and stays a skip under the flag,
because RIDE does not ship it. `make test TEST_FLAGS=--require-tools`;
`build.bat test` reads `%RIDE_TEST_FLAGS%`. A case written for another platform - gdb or lldb on
Windows, cl's debugger off it - is said as `not on this platform` and is not a
skip either: nothing is missing, the case is not this machine's.

## D13 / A7 - the artefact table and the generated page

- `tools/master-seal artefacts <bindir> [--release text]` adds (or replaces) a
  second table in `MASTER.SEAL`, **Shipped artefacts**, with the SHA-256 and
  size of every executable and library under `<bindir>` (`bin/` and
  `bin/lib/`, recursively), headed by the release's text and the master seal
  it was built from. `write` keeps an existing artefact table; `check` and
  `verify_seals.py` read only the source rows, so the source seal is unchanged
  by it. `release.sh` and `release.cmd` run it over the bin they packaged and
  leave the result as `MASTER.SEAL` beside `RELEASE.txt`.
- `tools/master-seal html [page]` rewrites the rows between `<!-- seals:
  generated by tools/master-seal --html -->` and `<!-- end seals -->` in
  `docs/ride-architecture.html` from `MASTER.SEAL`, and the master-seal
  figure and date in the page's sub-heading and footer with them.

## D16 / M9 - the pin

`release.sh` and `release.cmd` refuse when VM6747's Compiler-Cppi pin is not
the head of cpp11's default branch, printing both, unless `ALLOW_UNPINNED=1`.
`release.cmd`'s `RELEASE_BRANCHES` rehearsal warns instead, as it does for the
seals. `packaging/README.md` says so. `release.cmd` gains `RELEASE_DRYRUN=1` (stop after the
checks; with `RELEASE_BIN`, `build.bat confirm` over that bin) and `RELEASE_PIN`
(stands in for the pin in a dry run), so both refusals can be rehearsed.

## S2 - six libraries

`workspace.mk` already builds all six into `bin/lib/rts6x-tms6747` (four from
RTS6x's `all`, two packed from shalimar's runtime); `Makefile`'s
`DEPENDENCIES` and `build.bat confirm` now name all six, so `make confirm`
lists `printf6x.lib` and `printf6xd.lib` MISSING when they are, and both
release scripts run `confirm` and stop on MISSING.
