# WS-G handover - RIDE behaviour, release and seals

Branch `review/g-ride` of RIDE-4.7, off RIDE main `1faca4c` (WS-B merged). Nothing pushed, nothing
merged, no reseal and no version bump - the main session does RIDE 5.2. Decisions are in
`g-decisions.md` beside this note; what follows is what was built against them and measured.

## Commits

| commit | what |
| --- | --- |
| `9483d9b` | `g-decisions.md` (the previous agent's; amended in the last commit for D14's platform cases and the release dry run) |
| `560f115` | R5, D2, D5, P3 in `src/`, `winforms/`, `macos/` |
| `3f9f1a0` | tests: `--require-tools` (D14), the D2 states, R5 in the session suite, D5's lines |
| `662ec97` | S2, D16, D13, A7, M9: confirm, the release scripts, `tools/master-seal artefacts` and `html`, `packaging/README.md`, toolchain-check |
| `ecc45e8` | `src/help.cpp`'s four-targets one-liners; two comments R5/D2 made stale |
| `9c8eb2c` | step 9 of the plan: WS-B's passages amended for these decisions, the HTML manuals regenerated |
| last | this note and the `g-decisions.md` amendment |

## Every item

| item | state | how |
| --- | --- | --- |
| R5 / D1 / V13 | **done** | `runnerFor` (compile.cpp): Run on tms6747 is `RunSimulator` - the build links `<program>.out` (asm6x, lnk6x, RTS6x) and sim6747 runs it; `emulateOnly` turns it into Emulate. Build ▸ **Emulate on vm6747** replaces Run on Simulator in the console menu, WinForms and macOS; `--emulate` on the console (`--simulate` kept as `--run`'s synonym); `RIDE_RUN_EMULATOR = 3` in the bridge. The line before a run names the runner (`runnerLine`): `$ sim6747 --run p.out`, `$ vm6747 p.vm`, or for Verify both. The simulator takes no command line and says so. |
| D2 / R4 / M2 | **done** | `makeTiProgram` fails with the lines of `g-decisions.md`: no asm6x, `$ASM6X` gone, no runtime, TI's lib without `_eh`. `"emulateOnly": true` in the `.pro` (`Project::emulateOnly`, read and saved) builds no `.out` and says so; an Emulate run builds none either (`Toolchain::emulating`). |
| D5 | **done** | `nativeNames` gives the question's own words to the retry's line (`building again with X in place of the project's Y, as asked`) and its mark (`[built with X, not RIDE's Y]`, `[not built: X failed as well]`). |
| P3 / E7 | **done, as RIDE's pool** | `Recipe::commands` and `runRecipe`: a tms6747 group's per-source `-S` commands run at once on `min(sources, cores)` threads, output in source order, first failure's status. Not one cpp11 invocation: both drivers refuse `-S -o <dir>` with several inputs, and the Driver is WS-F's (decision in `g-decisions.md`). |
| D14 / V15 | **done** | `tests/test --require-tools`: a skip of RIDE's own tool or input is a FAIL naming it; TI's `rts6740_elf_eh.lib` is `external`; a case for another platform (gdb/lldb on Windows, cl's debugger off it) is `not on this platform`. `make test TEST_FLAGS=...`, `build.bat test` reads `%RIDE_TEST_FLAGS%`. |
| D16 / S1 / R10 / M9 | **done** | `release.sh` and `release.cmd` print cpp11's head and VM6747's pin and refuse when they differ unless `ALLOW_UNPINNED=1`; a `RELEASE_BRANCHES` rehearsal warns. `packaging/README.md` has a section on what the release scripts refuse. |
| S2 / T6 / A6 | **done** | `workspace.mk` already built all six; `Makefile` DEPENDENCIES and `build.bat confirm` now name `printf6x.lib`, `printf6xd.lib` (and `sim6747.exe` on Windows). `release.sh` stops on confirm through workspace.mk's `installer: confirm`; `release.cmd` runs `build.bat confirm` after the solution and stops on MISSING. |
| D13 | **done** | `tools/master-seal artefacts <bindir> [--release text]`: a second table after a `=` rule in `MASTER.SEAL`, SHA-256 and size of every executable and library (symlinks followed - the Mac app's `lib`), headed by the release text and the master seal. `write` keeps it; `check` and `verify_seals.py` read only the source rows. Both release scripts write it and leave `MASTER.SEAL` beside `RELEASE.txt`. |
| A7 | **done** | `tools/master-seal html [page]` rewrites the rows between the seal markers of `docs/ride-architecture.html` and the master-seal figure and date in its sub-heading and footer. Run on today's page it changes nothing - the hand copy was right. |
| help.cpp | **done** | "three languages, four targets, one core", "c90, four targets, DWARF or CodeView on three", "cpp11, four targets, and the host's compiler". |
| step 9 (WS-B's section 6) | **done here** | help/01 (3.5-5.1 paragraph), help/06 (step 4, "What each run proves", "When the `.out` is not made" - now the failure lines and `emulateOnly`), help/07 (Targets row, the Yes paragraph, "How a build runs"), help/10 (Build menu), manual Part I "What runs at once" and Part VI section 24, Express Help's target row, README's build section, the architecture page (tier 5 C6747 box, the `.out` line, the native-retry edge, "How a build runs"). guide.html, manual.html and EXPRESS-HELP-5.1.html regenerated with `docs2html.py`. The plan file itself (C++Optimize) is not edited - another repository. |

Found on the way and mended because the gates ran into them: `rts6xLinksTheOut`'s `.pro` named no
group, so it built nothing the moment its tools were beside it (it had only ever skipped);
`tests/session`'s tms6747 cases expected the 4.x "a .out needs TI's linker" line and
`shmrt/Runtime.obj`, both stale since RTS6x became the default in 5.1.

## Gate numbers (Windows box, a scratch folder, RIDE 5.1's installed tools copied into its `bin\`)

- `build.bat check` with `RIDE_TEST_FLAGS=--require-tools`, `CC1/CXX1/SHC/C2S` and
  `C6747_EHLIB=C:\ti\c6747-lib`: **tests/test 1419 checks, 0 failed, 0 skipped**; tests/session
  **364 checks, 0 failed**; `RIDE.exe --version` starts. (WS-B's run was 1232/0 and 200/0 with
  nothing beside the editor - every tool case now runs.)
- `tests/toolchain-check` (win.sh with `RIDE_BIN` = this bin): **9 of 9 programs agree** on every
  chain, now including c6000-run (RIDE's Run: RTS6x, sim6747) beside c6000-vm (Emulate);
  `tests/toolchain-check/RESULTS-2026-10-08.md`.
- GUI, Windows (scratch scripts `wsg-r5.ps1` → `wsg-qual.ps1`, schtasks /IT, this branch's `RIDE.exe`):
  c-demo and cpp-shapes as tms6747 projects - Build project links `<program>.out` against rts6x.lib;
  **Run project prints `$ sim6747 --run <program>.out` and the program's output**; **Emulate on
  vm6747 prints `[no .out: Emulate on vm6747 runs the assembly]` and `$ vm6747 <program>.vm`** with
  the same output; Verify: "the two agree, line for line". Results in `wsg-r5.jsonl` beside them.
- GUI, macOS (this branch's app, built from `macos/` into the scratchpad, inside a copy of the
  installed RIDE 5.1.app): cpp-shapes as tms6747 - Build ▸ Run Project ran `$ sim6747 --run
  cpp-shapes.out` and printed the four lines; Build ▸ Emulate on vm6747 ran `$ vm6747
  cpp-shapes.vm`, the same four lines, `[program returned 0]`.
- `release.cmd` dry runs (`RELEASE_DRYRUN=1`, fresh clones from GitHub): forced pin
  (`RELEASE_PIN=000…`) → **refused**, both commits printed, rc 1; a bin copy without `rts6xd.lib` →
  **confirm MISSING, refused**, rc 1; the same bin whole → checks pass, rc 0. GitHub's cpp11 head
  `2dd633b` is VM6747's pin today.
- `tools/master-seal artefacts` on the Mac app: 19 artefacts, six RTS6x/printf/shmrt libraries among
  them; `master-seal check` and `verify_seals.py` afterwards report only RIDE's 16 changed sealed
  files - the artefact table changes no source row; `write` keeps the table. MASTER.SEAL is not
  committed changed.
- Linux box: `tests/test` and `tests/session` build clean with g++ `-Werror`; the suite ran 1048
  checks, 1 failed - `findingInFiles`' "each hit with its line and column", which depends on
  directory order (find.cpp is untouched) - and 13 skipped (no tools there).
- `make comments`: RIDE has no such target; every comment group added in `src/` is at most three
  lines and one line before a single line of code.

## Sealed RIDE files changed (ride-5.1.dat; reseal at 5.2)

`src/compile.cpp`, `src/compile.h`, `src/editor.cpp`, `src/editor.h`, `src/help.cpp`,
`src/main.cpp`, `src/menu.cpp`, `src/menu.h`, `src/project.cpp`, `src/project.h`,
`src/toolchain.cpp`, `src/toolchain.h`, `winforms/MainForm.h`, `winforms/bridge.cpp`,
`winforms/bridge.h`, `macos/WindowController.mm` - sixteen, as `verify_seals.py` lists them.

## Left

- The VM6747 emulator's `Emulator/README` (V13) is VM6747's repository, not this one.
- A single-cpp11-invocation P3 waits on a driver that takes `-S -o <dir>` (WS-F's Driver); then
  `Recipe::commands` becomes one entry and nothing else moves.
- `release.sh` has no dry run; its pin refusal is the same comparison `release.cmd`'s was shown to make.
