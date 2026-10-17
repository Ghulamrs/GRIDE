# WS-B - RIDE documentation and the architecture page

Branch `review/b-docs` off RIDE main `644d6d7`. Workstream B of
`C++Optimize/docs/REVIEW-PLAN-2026-10-08.md`, answering the review
`CppOptimize-critical-review_1.md` sections 7.1-7.3 and the DOCUMENT rows of
sections 4, 5 and 6 that name B. Docs only: nothing under `src/` changed, and
`python3 tools/seal check` reads `seal ride 5.1: 80 files, 0 differ`.

## 1. Section 7.3, as each line read on 2026-10-08 before this branch

Quoted from `644d6d7`; the line numbers are the review's, re-read, with the
line as the file holds it today where the review's number has drifted.

- `help/07-building.md:49`: "`Ctrl-T` moves to the next target; the Target menu names all three."
- `help/07-building.md:53-56`: a three-row table - "| `x86_64-windows` | MASM, assembled by `ml64` |", "| `x86_64-linux` | GNU assembly |", "| `arm64-darwin` | this Mac's own |" - under "## Targets".
- `help/07-building.md:57-58`: "**Only the host's own target reaches a program.** c90, cpp11 and shalimar generate for all three".
- `help/README.md:33-34`: "| [C](c.md) | c90, three targets, DWARF on two of them |" and "| [C++](cpp.md) | cpp11, three targets, and the host's compiler by name |".
- `help/c.md:11`: "| targets | `x86_64-windows`, `x86_64-linux`, `arm64-darwin` |"; `help/c.md:22`: "## The three targets, and what reaches a program"; `help/c.md:28`: "On Windows c90 writes **MASM**, assembled by `ml64` and linked by `link`."
- `help/01-what-it-is.md:92`: "machine alike, with the same three targets and DWARF on the same two of them."
- `help/08-debugging.md:66`: "same behaviour on all four targets - including `x86_64-windows`" (the one page that had four).
- `help/cpp.md:14`: "| targets | `x86_64-windows`, `x86_64-linux`, `arm64-darwin` - the same three as c90 |"; `help/cpp.md:69-72`: "**cpp11 writes DWARF for `x86_64-linux` and `arm64-darwin`** ... On `x86_64-windows` it writes MASM and no line table, exactly as c90 does, and the Debug tab says so."; `help/cpp.md:82-83`: "So on Windows, C++ under cpp11 is where C under c90 is - no line table - and C++ under cl carries everything."
- `help/08-debugging.md:43`: "| c90 or cpp11 on `x86_64-windows` | **no** - MASM carries no line table |".
- `help/01-what-it-is.md:116-117` (the review's 110-111): "**It has no optimiser of its own.** What optimisation you get is whatever the compiler you chose does."; `help/07-building.md:79-82` meanwhile: "cpp11 optimises every target: on tms6747 `-O1` keeps locals in registers, schedules execute packets and fills delay slots, and `-O2` adds software pipelining, loop-invariant hoisting and inlining."; and `help/01-what-it-is.md` names none of tms6747, vm6747, sim6747, asm6x, lnk6x or RTS6x.
- `docs/ride-5.0-architecture.html:6` "<title>RIDE 5.0 Architecture</title>"; `:97` "RIDE 5.0 - compilers, tools and targets"; `:115-116` "RIDE 5.0 ... seal ride-5.0.dat"; `:281-292` the seals table reading `ride-5.0.dat 35BD9D0E`, `c90-1.1.dat B1317483`, `cxx1-1.5.dat D6B8B482`, `lnk6x-1.0.dat 1EABC53A`, `sim6747-1.1.dat DDB5751B`, `rts6x-1.0.dat 5AFCD02F`; `:297` "master seal 1732D0A4 is stale until the next release". `MASTER.SEAL` the same day: `ride-5.1.dat 0A46C214`, `c90-1.2.dat 8D82BAA5`, `cxx1-1.6.dat 2891841F`, `lnk6x-1.1.dat 1D45D6CE`, `sim6747-1.2.dat 2D1A3C81`, `rts6x-1.2.dat 040CE785`, master `DC1EF907`. `src/about.cpp:28` returns "5.1". No `ride-5.0.dat` exists in the tree (`ride-4.5`, `4.51`, `4.7`, `5.1`).
- `help/06-the-project.md:125-127`: "installed in `bin/lib/rts6x-tms6747` in two builds - `rts6x.lib` at -O2 for a Release build and `rts6xd.lib` at -O0 with `_DEBUG` for a Debug one, with `printf6x.lib` and `printf6xd.lib`" (T6: the page is kept; S2 is G's and makes it true).
- T7 and T8 are cpp11's documents and WS-A's.

Also found stale while re-reading, outside the review's list: `help/manual/`
(the fifteen-part Complete Manual) says "no line table in the MASM spelling",
"ml64" as the IDE's assembler and "vm6747 assembles and runs the .s" in Parts
I, II, V, VI, VII, XI and XV; `README.md:221-225, 274-275, 335-350, 436-441,
704-706, 742-744` carry the pre-M10 Windows debugging story and "three
targets"; `src/help.cpp:11,21,22` (the Help > Contents one-liners) say "three
targets" and is `src/`, which this workstream does not touch.

## 2. What was done, commit by commit

| commit | files | what |
| --- | --- | --- |
| `aae2064` | `docs/review-2026-10-08/ws-b.md` | section 1 above |
| `9d8e155` | `help/01, 06, 07, 08, 10, README, c.md, cpp.md` | four targets in every table; help/07 "What assembles and links a Windows program" (Debug = `-masm=gnu`, clang's assembler, `link.exe /DEBUG`, cdb; Release = `-masm=masm` when Tools names an assembler, RIDE's masm and link), "When the project's own tools fail" (the D5 question verbatim, and that a Yes marks nothing), "How a build runs, and what runs at once", "What stands behind each tool"; cpp.md and c.md: CodeView on x86_64-windows since 5.1, `-O2` in release; help/08: the debug matrix with cdb for a Debug build and the tms6747 row; help/01: the 3.5-5.1 paragraph naming asm6x, lnk6x, RTS6x, vm6747, sim6747, masm, link, and the optimiser bullet; help/06: what a Run on vm6747 proves and cannot, Run on Simulator and Verify, the two cases that stop short of a `.out` without failing today |
| `e309475` | `help/manual/01, 02, 05, 06, 07, 11, 15`, `packaging/README.md` | the Complete Manual's Parts I, II, V, VI, VII, XI, XV brought to the same facts (Part V chapter 22 rewritten: "The two Windows spellings, and how a Windows program is debugged"; Part VI on what vm6747 cannot see; the "what runs at once" paragraph in Part I); packaging/README names c90, cpp11, shalimar and the nine tools rather than cc1, cxx1, shc |
| `27c01d1` | `README.md` | the toolchain paragraphs at 220-228, 246-252, 274-277, 331-346, 438-442, 704-705, 742-743 |
| `c3a4ce1` | `docs/ride-architecture.html`, `docs/ride-5.0-architecture.html` (redirect) | the page: title and banner 5.1, the folder name explained, c90-1.2 and cxx1-1.6, CodeView.cpp/CodeViewTypes.cpp and the four target files listed, the x86_64-windows box as Debug / Release / prompted retry, the tms6747 box with lnk6x default against TI's, RTS6x and shmrt6x as cpp11's output, the vm6747 sentence, the board "via Code Composer Studio, not RIDE", an "Edges the page used to leave out" list (cpp11→RTS6x, cpp11 standalone→TI lnk6x+rts6740, Debug→clang→link.exe→cdb, the prompted retry), "How a build runs", "What verifies each tier", the matrix with CodeView.cpp and Dwarf.cpp, the installed tree under `RIDE 5.1`, the seals copied from MASTER.SEAL between `<!-- seals: generated by tools/master-seal --html -->` and `<!-- end seals -->` |
| `83a32d6` | `help/guide.html`, `help/manual.html`, `help/06`, `help/07`, the page | the two HTML manuals regenerated by `packaging/windows/docs2html.py` with the 5.1 titles (there is no `make help`; this script is the renderer, run as `build-installer.sh` runs it); D15 named in both tier tables; cpp11's two link lines in help/06 |

## 3. Every item, with its state

| item | state | where |
| --- | --- | --- |
| D3 (Debug bypasses MASM/LINK; manual said the opposite) | done | help/07, cpp.md, c.md, 08, manual Parts I/II/V/VII/XI/XV, README.md, the page |
| D1 disclosure (F5 runs the `.s` on vm6747 with its own libc/EH) | done for today's behaviour; **to amend after G** (Run default) | help/06 "What each run proves", manual Part VI section 24, the page's run tier and vm6747 row |
| D5 (prompted native fallback marks nothing) | done for today's behaviour; **to amend after G** (the log line) | help/07 "When the project's own tools fail", the page's x86_64-windows box and edges list |
| A1 backend file list | done | the page, tier 2 and the matrix |
| A2 x86_64-windows row | done - Debug / Release / prompted retry | the page, tier 4 |
| A3 F4/F5 path | done for today: Run = vm6747 on the `.s`, Run on Simulator = sim6747 on the `.out`; **to amend after G** | the page tier 5, help/06, help/10 |
| A4 vm6747 sentence | done | the page tier 5, help/06, manual Part VI |
| A5 the board | done - "via Code Composer Studio, not RIDE" | the page tier 5; help/01 "What it will not do" |
| A7 seals table | done for the values of 08-10 (copied from MASTER.SEAL); the generator marker is in place for G's `master-seal --html`; title, banner, footer 5.1; the folder name explained | the page |
| A8 cpp11 → RTS6x, shmrt | done | the page, tier 4 and the edges list |
| A9 cpp11 standalone → TI lnk6x + rts6740, which is default | done for today (RIDE: lnk6x + RTS6x; cpp11 alone: TI's); **to amend after F's D9** | the page edges list, help/06 "Two link lines" |
| A10 prompted retry edge | done | the page |
| A11 Debug → clang → link.exe /DEBUG → cdb | done | the page, help/07, help/08, cpp.md, c.md |
| A12 parallelism | done for today (cpp11's pool; RIDE's serial `&&` for tms6747); **to amend after G's P3** | the page "How a build runs", help/07, manual Part I |
| A13 verification table | done as a hand-written table of record, marked to be regenerated from WS-H's verification page | the page "What verifies each tier", help/07 "What stands behind each tool" |
| A14 ml64 never named by RIDE | done | help/07, manual Parts I/V/VII/XV, the page |
| M1 help/06:98 listed asm6x in the F5 run | done | help/06 step 4 |
| M2 D2 wording | done for today's behaviour (asm6x refusing or lnk6x failing fails the build; no asm6x beside the editor, or asm6x with neither RTS6x nor TI named, ends without a `.out` and reports success); **to amend after G's D2** | help/06 "When the `.out` is not made" |
| M3 D3 contradicted by three pages | done (T2, T3) | as D3 |
| M4 D15 undisclosed | done - the MASM spelling's own coverage named; the diagram says when `-masm=masm` is passed | help/07 and the page's tier tables; **F fixes D15** |
| M5 D9 in RIDE's docs | done for today | help/06 "Two link lines" |
| M6 D5 in help/07 | done | help/07 |
| M7 parallelism in RIDE's docs | done | help/07, manual Part I, the page |
| M8 stage verification in RIDE's docs | done | help/07, the page |
| T1 three targets | done | help/07:49,53-56; help/README.md:33-34; help/c.md:11,22; help/cpp.md:14; help/01:92; help/10:74; README.md |
| T2 help/07:53 | done | help/07:53 and the section under it |
| T3 cpp.md:70-73,82 and 08:43 | done | cpp.md:69-78, 90-93; help/08:43-44 |
| T4 optimiser line, help/01 never naming the C6747 half | done | help/01:105-123 (the 3.5-5.1 paragraph) and :136-139 |
| T5 page title / seals / 5.0.dat | done | the page |
| T6 help/06:125-127 | kept as the plan says; **G's S2 makes it true** | help/06:128-130 |
| E7 RIDE README parallelism | done in `help/07` and the page; RIDE's `README.md` has no build-internals paragraph to carry it and gained none - the manual is the user's document and `README.md`'s building section describes the editor's own build | - |
| V13 `VM6747/Emulator/README` | **not done here**: another repository, shared with G. The sentence to add: "vm6747 reads the compiler's assembly text and runs it over a C library and an exception-handling runtime of its own; it never sees asm6x's encoding, lnk6x's image or RTS6x, so a fault in any of those runs clean here and is sim6747's to find." | - |

## 4. Section 7.3, as each line now reads

- `help/07-building.md:49`: "`Ctrl-T` moves to the next target; the Target menu names all four."
- `help/07-building.md:53`: "| `x86_64-windows` | Debug: clang's assembler and `link.exe /DEBUG`; Release: RIDE's own `masm` and `link` | see below |"; `:56`: "| `tms6747` | RIDE's own `asm6x` and `lnk6x`, against RTS6x | the TI C6747; runs on `vm6747` and `sim6747` on any host |".
- `help/README.md:33-34`: "| [C](c.md) | c90, four targets, DWARF on two of them and CodeView on Windows |", "| [C++](cpp.md) | cpp11, four targets, and the host's compiler by name |".
- `help/c.md:11`: "| targets | `x86_64-windows`, `x86_64-linux`, `arm64-darwin`, `tms6747` |"; `:22`: "## The four targets, and what reaches a program".
- `help/cpp.md:14`: "| targets | `x86_64-windows`, `x86_64-linux`, `arm64-darwin`, `tms6747` — the same four as c90 |"; `:73`: "**On `x86_64-windows` it writes CodeView**, since RIDE 5.1: a Debug build is ..."; `:90-91`: "So on Windows, C++ under cpp11 and C under c90 are debugged by cdb through CodeView, exactly as C++ under cl is."
- `help/01-what-it-is.md:92`: "machine alike, with the same targets and DWARF on the same two of them."; `:136`: "- **It has no optimiser of its own, and does not want one.** The editor optimises nothing; what a Release build gets is the compiler's — cpp11's `-O1` and `-O2` on every target, c90's on the x86-64 targets, nothing from shalimar"; and `:105-123` is the paragraph naming tms6747, asm6x, lnk6x, RTS6x, vm6747, sim6747, masm and link.
- `help/08-debugging.md:43`: "| c90 or cpp11 on `x86_64-windows` | cdb, reading the CodeView a Debug build carries — since 5.1; a Release build is MASM, which has no line table, and cannot be debugged |"; `:44`: "| c90 or cpp11 on `tms6747` | **no** — no line table, and the emulator and the simulator are not debuggers |".
- `docs/ride-architecture.html:6` "<title>RIDE Architecture</title>"; `:97` "RIDE 5.1 - compilers, tools and targets"; `:118` "RIDE 5.1"; `:327-338` the seals as MASTER.SEAL holds them, `ride-5.1.dat 0A46C214` ... `rts6x-1.2.dat 040CE785`, master DC1EF907 in the footer. `docs/ride-5.0-architecture.html` is a redirect to it.
- `help/06-the-project.md:128-130` still promises `rts6xd.lib` and `printf6xd.lib` in the install - deliberately; S2 (G) makes the dev `bin/` match the page.

## 5. Gates

- `python3 tools/seal check`: `seal ride 5.1: 80 files, 0 differ` after every commit (nothing under `src/`).
- The two HTML manuals regenerated by the renderer that `packaging/windows/build-installer.sh` uses (`docs2html.py`), clean; `docs/ride-architecture.html` parses with every tag balanced.
- Windows box, a scratch folder (a `core.autocrlf=false` clone of the branch from a bundle): `build.bat test` → "the manual, and what the editor says is in it" passes, **1232 checks, 0 failed**; `build.bat session` → **200 checks, 0 failed** (a clone with no compilers beside the editor, so the session suite is its short form; a docs-only branch cannot change it). `tests/toolchain-check` not run - it wants the tools beside the editor, and nothing here reaches it.
- `tests/check-help.sh` untouched: Appendix A was not edited.

## 6. What waits for G (and F, H), and the exact passages to amend

- **Run default (R5)**: help/06 step 4 ("Run project runs the assembly on vm6747") and "What each run proves"; help/07 Targets table's tms6747 row; help/01's 3.5-5.1 paragraph ("`F5` runs on the emulator, Build ▸ Run on Simulator on the simulator"); help/10's Build menu line; manual Part VI section 24 ("In the editor, F5 / Run project on `tms6747` builds with `-S` and runs on `vm6747`"); the page's tier 5 C6747 box. If G makes F5 = sim6747 on the `.out` and adds "Emulate on vm6747", every one of those swaps the two names and the new menu item goes into help/10.
- **D2 wording**: help/06 "When the `.out` is not made" describes today's two silent cases; once G fails the build (unless `emulateOnly`), the paragraph becomes the new failure lines and the `emulateOnly` setting.
- **D5 log line**: help/07 "What a Yes does not do today is mark the program it made" becomes the line G writes; the page's edges list likewise.
- **P3**: help/07 "How a build runs" second bullet, manual Part I's "What runs at once", the page's "How a build runs" - all say a tms6747 group compiles serially today.
- **A7 generator**: the rows between `<!-- seals: generated by tools/master-seal --html -->` and `<!-- end seals -->` in the page are G's to write; the hand copy there is MASTER.SEAL's of 08-10 and is right today.
- **A9 / M5 after F's D9**: help/06 "Two link lines for one compiler" and the page's second edge.
- **A13 after H's verification page**: regenerate the two tier tables from `C++Optimize/docs/VERIFICATION-2026-10-09/`.
- **Outside this workstream, found while re-reading**: `src/help.cpp:11,21,22` - the Help > Contents one-liners the editor prints - still say "three languages, three variants, one core", "c90, three targets, DWARF on two of them", "cpp11, three targets, and the host's compiler"; `src/` is sealed and G's. The product's `About` and the page now agree on 5.1; `help/README.md`'s first line says 5.1 too.
