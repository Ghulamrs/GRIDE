# RIDE 5.17 — Express Help

Three languages, four targets, one editor on three systems - a window on
Windows and on macOS, a console editor on all three, and the project's own
compilers, assemblers, linkers and C6000 emulator behind them. This is the
quick reference; the full pages are under `help/`.

--------------------------------------------------------------------------
## 1. What is installed, and where

| System  | Installer                          | Window (GUI)        | Console editor              |
|---------|------------------------------------|---------------------|-----------------------------|
| Windows | `RIDE-5.17.msi`                     | `RIDE.exe`          | `RIDEConsole.exe`           |
| macOS   | `RIDE-5.17-macos.pkg` (macOS 12+, Apple silicon) | **RIDE 5.17** in Applications | `ride-5.17` and `ride` (in `/usr/local/bin`) |
| Linux   | `RIDE-5.17-linux-x86_64.run` (Ubuntu 22.04+, Debian 12, RHEL 9, Amazon Linux 2023) | - | `ride-5.17` and `ride` |

The programs RIDE drives, the same on all three:

| Program     | What it is                                              |
|-------------|---------------------------------------------------------|
| `c90`       | the C compiler - ISO C 90                               |
| `cpp11`     | the C++ compiler - ISO C++ 11 (the subset in `docs/EXCLUSIONS.md`) |
| `shalimar`  | the Shalimar compiler - Shalimar 1.2                    |
| `masm`      | x86-64 assembler (answers ml64's command line too)      |
| `asm6x`     | TMS320C6000 assembler                                    |
| `link`      | x86-64 linker (Windows objects)                         |
| `lnk6x`     | TMS320C6000 linker                                      |
| `vm6747`    | the TMS320C6747 emulator                                |
| `c2s`       | C-to-Shalimar converter                                 |

On Windows they are `.exe` files in `bin\`; on macOS they are in
`/usr/local/ride-5.17/bin` and on Linux in `/opt/ride-5.17/bin`, on `PATH` by
name twice: `cpp11-5.17` always, and `cpp11` pointing at the newest
installed. RIDE 5.17.app on macOS carries its own copy of every one inside the
app. 5.17 replaces 5.1 (or 5.0), and installs beside 4.5, 4.51 and 4.7 and leaves them where they were.

The sample CCS 7.4 projects - Hello, Sample and SampleExt - are in
`projects/ccs`, beside the other sample projects in `Documents/RIDE/projects`; its README says how RIDE opens one and what each prints.

--------------------------------------------------------------------------
## 2. Languages and targets

| Language | Files         | Compiler   | Debug info                       |
|----------|---------------|------------|----------------------------------|
| C        | `.c` `.h`     | `c90`      | DWARF on Linux and macOS         |
| C++      | `.cpp` `.hpp` | `cpp11`    | DWARF on Linux and macOS         |
| Shalimar | `.shl`        | `shalimar` | none; its debugger stops by line |

The suffix picks the language; the **Language** menu overrides it for a file
whose name says otherwise. Each compiler announces itself on every compile
(`©2026 G. R. Akhtar - ISO C++ 11`).

**The four targets** (Target menu, or Ctrl-T to cycle). A target the machine
is not builds to assembly only (`-S`) and says so; tms6747 runs everywhere, on
the emulator:

| Target           | Runs on                                   |
|------------------|-------------------------------------------|
| `x86_64-windows` | Windows                                   |
| `x86_64-linux`   | Linux                                     |
| `arm64-darwin`   | macOS (Apple silicon)                     |
| `tms6747`        | Run: **sim6747** on the linked `.out`; Build ▸ Emulate on vm6747: the **vm6747** emulator - on every system |

**Shalimar assigns with `:`**, never `=`: `x : 2`, `int n : 5`. `=` compares.
Writing `int x = 2` is answered `Unexpected '=' use ':'`.

--------------------------------------------------------------------------
## 3. Debug and Release: optimization

**Build ▸ Configuration ▸ Debug / Release** (Ctrl-D in the console
editor) chooses how everything is compiled. The status bar shows which.

| Compiler          | Debug              | Release               |
|-------------------|--------------------|-----------------------|
| `c90`, `cpp11`    | `-g -D_DEBUG=1`    | **`-O2 -DNDEBUG=1`**  |
| host `c++` group  | `-g -D_DEBUG=1`    | `-O2 -DNDEBUG=1`      |
| `cl` group        | `/Od /Zi`          | `/O2`                 |
| `shalimar`        | `--debug`          | (no `-O`)             |

- Debug is `-O0` - no optimisation - and carries debug information.
- Release optimises: `cpp11 -O2` runs its optimizer (register allocation,
  copy propagation, loop alignment, inlining); for **tms6747** it also
  schedules the C6000's instructions to their delay slots. `-O1` and `-O2` are
  the same passes today.
- **The choice belongs to the machine, not the project**: it is kept in
  `~/.ride/state.json`, and a `"config"` key in a `.pro` is ignored - a project
  that travels does not put everyone who opens it into Release.
- On the command line: `cpp11 -O2 prog.cpp -o prog` (or `-O1`, or `-O0`, the
  default); `c90 -O2` the same.

--------------------------------------------------------------------------
## 4. Where your work goes, and the samples

| Menu                                  | Starts in                    |
|---------------------------------------|------------------------------|
| Project ▸ New / Open / Save As        | `Documents/RIDE/projects`    |
| File ▸ Open / Save As (a new file's first Save too) | `Documents/RIDE/programs` |

Both folders are made the first time they are needed, and **filled with
samples** when they are new or empty - nothing already there is ever replaced:

- **projects** - `c-bank`, `c-demo`, `c-example`, `c-stats` (C); `cpp-inventory`,
  `cpp-shapes`, `cpp-table`, `cpp-vector3`, `compilerpp` (C++); `shl-primes`,
  `shl-matrix` (Shalimar, where a program calls functions from the project's
  other file with nothing to declare); `thirdparty-mathx`, the third-party
  library template (section 7); and `ccs` - Hello, Sample and SampleExt, three
  CCS 7.4 projects for the C6747.
- **programs** - `hello.c`, `fibonacci.c`, `projectile.c`, `hello.cpp`,
  `smart.cpp`, `words.cpp`, `hello.shl`, `gcd.shl`, `primes.shl`, `rotmat.shl`,
  `table.shl`: open one and **Run File**.

--------------------------------------------------------------------------
## 5. Making and building a project

**Project** menu: New…, Open…, Recent, Save As…, Close; New File, Add
File (the one in front), Remove, Rename…, Delete…; Include Paths…,
Libraries…. The file items act on the open
project's files and are offered only while a project is open.

**Build** menu: Compile File (Ctrl-B), Run File (F5), Build Project (F4), Run
Project, Stop, Clean (removes the program, its .pdb, .ilk, .out, .dSYM and .vm,
and empties the panes - never a source or the .pro), Configuration ▸ Debug / Release, Convert C ⇄ Shalimar, Jump to Next
Issue, Clear Issues.

The bottom panel shows **Errors**, **Progress** and **Output**. The title bar
shows `RIDE 5.17 - <project> - <file>`, the status bar the language, the
configuration, the compiler (`*` when the file chose it) and the target.

On macOS the menus are in the menu bar at the top of the screen **and** in a
row inside the window; About RIDE is in the RIDE menu, as on every Mac app.

--------------------------------------------------------------------------
## 6. The project file (`.pro`)

One JSON object; `//` comments are allowed.

    {
      "name": "demo",
      "toolchain": "auto",           // auto | c90 | cpp11 | shalimar | c++ | cl
      "arch": "x86_64-windows",      // the target: one of the four
      "include": ["third_party/x/include"],      // extra header directories
      "libraries": ["third_party/x/libs/x.lib"], // extra objects and libraries
      "groups": {
        "Sources": ["main.c", "greet.c"],
        "Headers": ["greet.h"]
      },
      "build": { "target": "demo", "groups": ["Sources"] }
    }

- **groups** name the files shown in the left pane; a group may set its own
  `"toolchain"` (e.g. `"c++"` for the host's compiler).
- **build** names the program the build makes (`target` - `demo`, or
  `demo.exe` on Windows) and which groups go into it.
- **Shalimar**: every file has a `main()`; `target` names the file whose
  `main()` is the program, and a call to a function another project file
  defines is found there.
- No `build`: nothing is built, and Ctrl-B still compiles the open file.

--------------------------------------------------------------------------
## 7. A third-party library: include path and binaries

A library usually arrives as a header directory and prebuilt binaries. The
project file carries the whole provision:

- `"include"` - its header directories; every compile gets each as `-I`.
- `"libraries"` - its binary files (`.a` / `.o` on macOS and Linux, `.lib` /
  `.obj` on Windows); each is handed to the link. Name each file.

Both lists are relative to the project, and **Project ▸ Include Paths… /
Libraries…** edit them. **Option ▸ Shared Include Paths… /
Shared Libraries…** hold the same two lists for every project, in
`settings.json`.

**`thirdparty-mathx`** is the worked template: `third_party/mathx/include`, a
`libmathx.a` built by clang for macOS and a `mathx.lib` built by Visual
Studio for Windows. Open `mathx-macos.pro` on a Mac and `mathx-windows.pro`
on Windows - they differ only in `"arch"` and the one library. A library made
by another compiler links when its interface is C (`extern "C"`); one that
passes `std::string` or `std::vector` across its interface does not, since
RIDE's C++ library is its own.

--------------------------------------------------------------------------
## 8. Windows: the assembler and clang

Inside RIDE, `settings.json` names **masm** as the x86_64-windows assembler,
so RIDE builds need nothing from Microsoft to assemble. On the **command
line**, `cpp11` writes the GNU spelling by default, which **clang** assembles
- the only spelling that lets a program of several C++ files share inline
functions (COMDAT). `cpp11` finds clang in any Visual Studio 2022 edition that
has the **C++ Clang tools** component (Visual Studio Installer ▸ Individual
components), in a standalone LLVM, or on `PATH`; with none, it assembles with
`masm` instead and says so - enough for a one-file program.

To use `masm` yourself, ask for its spelling:

    cpp11 -S -masm=masm hello.cpp -o hello.asm
    masm -t x64 hello.asm -o hello.obj          (or masm /c /nologo /Fo hello.obj hello.asm)

A `.s` from plain `cpp11 -S` is clang's spelling and `masm` will not read it.

**The link**: `link` and `lnk6x` are RIDE's own. When one of them itself fails a
build - not for the program's own link error, such as a missing `main` - RIDE asks whether to use Visual Studio's `link.exe`
(or TI's `lnk6x`) for that build - found through vswhere or the directory
**Option ▸ Linker for tms6747…** names; `"askNative": false` in
`settings.json` never asks.

**Real C674x silicon (.out / .hex)**: `bin\ti\ti-build.cmd <program>` builds a
linked `.out` and Intel `.hex` with a Texas Instruments Code Generation Tools
install already on the machine (none of TI's tools ship with RIDE); see
`bin\ti\TI-BUILD.txt`.

--------------------------------------------------------------------------
## 9. Where things are

**Windows** (`C:\Program Files\RIDE` by default)

    bin\           RIDE.exe, RIDEConsole.exe, and the nine programs of section 1
    bin\lib\       the Shalimar runtimes (x86-64 and tms6747)
    bin\ti\        ti-build.cmd - the real-silicon TI path
    include\       cpp11's headers: C++ (<vector>, <string>, <new>, …) and the C ones
    lib\           c90's headers (<stdio.h>, <string.h>, …)
    projects\, programs\   the samples copied into Documents\RIDE
    examples\, help\       worked programs; the full help pages
    settings.json  the installation's settings (next section)

**macOS**: RIDE.app in Applications holds everything the window needs;
`/usr/local/ride` holds the console editor and the same `bin`, `include`,
`lib`, `help`, `projects` and `programs`, with the commands linked into
`/usr/local/bin`. **Linux**: `/opt/ride` (or `--prefix`), laid out the same.

**settings.json** - the include paths and libraries every project gets, the
default compiler, the assembler and linkers, `"askNative"`, the code font:

| System  | settings.json - yours, to change       |
|---------|----------------------------------------|
| Windows | `%USERPROFILE%\RIDE 5.17\settings.json`  |
| macOS   | `~/.ride/settings.json`                |
| Linux   | `~/.ride/settings.json`                |

It is made the first time RIDE starts, from the installation's own
`settings.json`, which stays as the defaults: whatever yours does not set is
read from there. In the Windows window, **Tools ▸ Settings file...** opens it.

What you were last doing - recent files and projects, Debug or Release - is
in `~/.ride/state.json` on every system.

--------------------------------------------------------------------------
## 10. Removing RIDE

- **Windows**: Settings ▸ Apps ▸ RIDE ▸ Uninstall.
- **macOS**: `sudo rm -rf /Applications/RIDE.app /usr/local/ride`, the links in
  `/usr/local/bin` (`ride c90 cpp11 shalimar c2s vm6747 asm6x masm lnk6x`), and
  `sudo pkgutil --forget com.ghulamrs.ride`.
- **Linux**: `/opt/ride/uninstall.sh` (or `<prefix>/uninstall.sh`).

`Documents/RIDE` is yours and is never removed.

--------------------------------------------------------------------------
## The full manual

This is the quick reference. The complete manual - the compilation model,
every language feature and non-feature, the project file, the command-line
reference, the targets and native tools, troubleshooting - is in
`help/manual/` (Part I begins in `01-part1-model.md`); the Shalimar language
is specified in `help/appendix-a-shalimar-language.md`, and calling C from
Shalimar in `help/mixing-c-and-shalimar.md`.
