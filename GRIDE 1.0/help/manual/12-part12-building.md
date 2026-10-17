# Part XII — Building RIDE and its compilers from source

You do not need this to *use* RIDE — the installer gives you built binaries.
This part is for building the editor and the compilers yourself, understanding the
workspace, and the verification discipline the project holds itself to.

--------------------------------------------------------------------------------
## 60. The pieces and where they live

RIDE is one repository; the compilers are their own repositories beside it:

    RIDE            the editor, the workspace build, the packaging
    Compiler-C  (cc1)  the C compiler        (i-line: Compiler-Ci → c90)
    C++         (cpp11) the C++ compiler      (i-line: C++Optimize → cpp11)
    Compiler-S  (shc)  the Shalimar compiler (i-line: Compiler-Si → shalimar)
    Converter-C2S (c2s) the converter
    Emulator    (vm6747) the C6000 emulator (i-line only)

The *i-line* (the `…i` binaries) is what RIDE drives since 3.5 — the same compilers
with the fourth target (tms6747) added, built beside the originals so the
originals stay sealed. RIDE 3.0 drove the originals.

--------------------------------------------------------------------------------
## 61. One command per platform

Each platform opens one thing and builds all of it, editor and compilers
together, into one directory (`bin/`):

- **macOS** — `RIDE.xcworkspace` (opens the editor and every compiler at
  once), or `make -f workspace.mk`.
- **Windows** — `RIDE.sln`, or `build.bat solution`.
- **Linux** — `make -f workspace.mk`.

The reason they build together is the reason for a workspace at all: a change to a
compiler and the change to the editor that goes with it are one build and one
issue list. The output lands in `bin/` so the editor finds the compilers it
drives beside itself.

**`make -f workspace.mk`** builds `c90`, `cpp11`, `shalimar`, `vm6747`, `c2s` and the
editor into `bin/`, builds the Shalimar host runtime and (via `cpp11`) the C6000
runtime, and finishes with a `confirm` step that checks everything the editor
drives is actually present — because "built" and "usable" are different states,
and a compiler without its runtime beside it passes a build and fails at the
first link.

**`build.bat`** on Windows: `build.bat` alone builds the console editor;
`build.bat solution` builds the whole `RIDE.sln` (into `bin\` via
`/p:OutDir`); `build.bat gui` builds just the window; `build.bat check` runs the
suites; `build.bat product` lays down the install tree.

--------------------------------------------------------------------------------
## 62. The relay scripts

Development happens on more than one machine. Two scripts relay a Mac checkout to
the other boxes and build there:

- **`tools/to-windows.sh`** — tars the editor and the i-line, copies them to the
  Windows box as siblings, and builds `RIDE.sln` and runs the suites. The box
  has no `make`; the solution is the build.
- **`tools/to-linux.sh`** — relays to the Linux box and runs
  `make -f workspace.mk`.

They exist because a relayed tree is not a checked-out one: a file copied wrong is
a stale build with a green suite in front of it, so the scripts are careful about
what travels and re-run the suites on the far side.

--------------------------------------------------------------------------------
## 63. The generated project files

The IDE project files are **generated**, not hand-kept, by
`RIDE/tools/make-projects.py` from each compiler's Makefile:

    python3 tools/make-projects.py            write them
    python3 tools/make-projects.py --check    say whether they are current

A hand-kept project drifts — someone adds a source to the Makefile, forgets the
project, and the IDE quietly builds a smaller program with no error. `--check`
rebuilds every project in memory and compares, catching exactly that. Two
projects are kept by hand and only checked (the window's `RIDEGui.vcxproj`,
which compiles one file managed and the rest native, and `cc1.vcxproj`, which
belongs to another repository); their *source lists* are still checked against the
Makefiles.

The generator also writes shalimar's runtime build into `shc.vcxproj`'s post-build step
and Xcode phase — including the C6000 runtime, which `cpp11` emits — so the
Shalimar runtime is built beside `shalimar` wherever the solution or workspace builds.

--------------------------------------------------------------------------------
## 64. Verification — three boxes, and proving the artefact

The discipline the project holds to, worth adopting if you build on it:

- **Three boxes before a release.** Mac, Linux and Windows each build and run the
  suites; while iterating, the Linux leg alone is the quick check. A change is not
  done until all three are green, because each catches what the others cannot —
  the Mac's library hides non-standard C++ that only real g++ refuses; the box is
  the only place the MSVC ABI and `ml64` are exercised; the emulator is
  host-independent but the strict-C++14 judgement is the Linux box's.
- **Prove the artefact, not the exit status.** A green suite proves nothing until
  you know what it ran against: `make` with nothing to rebuild, a relay that
  silently failed, or a binary older than its sources all report success. Check
  the binary is newer than its sources; grep the emitted assembly for a token the
  change introduces; and the fingerprint suites compare every byte of every
  target's assembly against recorded digests, which is what says a change that
  should alter nothing did.
- **The fourth sandbox.** For tms6747, TI's own tools on the Windows box assemble
  and link our output (`Emulator/tests/ti.sh`), proving binary compatibility with
  the real toolchain — a check no emulator can make of itself.

None of this is needed to use the installers; it is why the installers can be
trusted.
