# GRIDE

Two Visual Studio-style IDE front-ends for the C90 / C++11 / Shalimar toolchain (targets Windows, Linux, macOS, TMS320C6747). They will ship side by side, probably with one installer. Both are C++/CLI on .NET Framework 4.8 and build only on Windows.

## Machines

- **This Windows box is the primary reference.** Code is edited, built and run here, in `C:\Users\GRA\work\GRIDEd`.
- **The Mac is only the git bridge to GitHub** (`Ghulamrs/GRIDE`). Do not edit or build on the Mac.

## Layout of `C:\Users\GRA\work\GRIDEd`

| Folder | What it is | In this repo's git |
|---|---|---|
| `GRIDE 2.0\` | The compact IDE (one translation unit, `GRIDE.sln`) | yes |
| `GRIDEd\` | The layered IDE, the active one (`GRIDE.sln`, `GRIDE\src`, `docs`, `samples\cppparse`) | yes |
| `Compilers\` | `Compiler-Ci` (c90), `C++Optimize` (cpp11), `Compiler-Si` (shalimar), `Converter-C2S` (c2s) | no - each is its own repo |
| `TI\` | `ASM6x`, `LNK6x`, `RTS6x`, `SIM6747`, `VM6747` | no - each is its own repo |
| `Windows\` | `MASM`, `LINK` | no - each is its own repo |

`Compilers\`, `TI\` and `Windows\` are excluded in `.git\info\exclude`, not `.gitignore`. Their own scripts expect them as siblings, as they are here. Those repos came from the Mac and need `core.filemode false` (already set).

## Build GRIDEd

`GRIDEd\GRIDE.sln` holds GRIDE and all eleven tool projects (solution folders Compilers, TI, Windows), so Visual Studio 2022 builds the whole tool chain. Open it and build `Release | x64`, or from the command line:

```
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.com" GRIDEd\GRIDE.sln /Build "Release|x64"
```

Everything builds into `GRIDEd\x64\Release`: GRIDE.exe, the ten tool exes, `toolchain.json`, `docs`, and `lib\` (Shalimar runtimes, `rts6x-tms6747`, `ccs`). GRIDE is ready to run there, with no staging. Close GRIDE before rebuilding. It must build with 0 warnings: Release|x64 does, 0 errors and 0 warnings in the IDE (2026-10-15).

**The dependency tree is in `GRIDEd\README.md`** (section 1, *The tool chain in the solution*, text tree plus `docs\solution-dependencies.svg`). Keep it, and this file, in step with `GRIDE.sln`. In short: GRIDE depends on all 11 tools; shc (shalimar) on cxx1 (cpp11); rts6x on cxx1, asm6x and shc. Solution Explorer shows each project file's own name: cc1 is c90, cxx1 is cpp11, shc is shalimar.

`Debug | x64` stops at link's C2220 (`Windows\LINK\src\image.cpp`, C4701 under its `/WX`), and cxx1 warns C4702 (`A64Peep.cpp`). These are known and left as they are; do not fix them.

## Development tools

GRIDE finds each tool by `toolchain.json`'s lookup: environment variable, tool folder, **beside GRIDE.exe**, RIDE install, PATH. The solution build puts every tool beside GRIDE.exe. The old manual staging into `GRIDEd\bin\x64\Release` is retired; that folder is stale.

Each tool repository's own solution or project (`shc.sln`, `asm6x.vcxproj`, ...) still builds that tool alone, into its own folder. RTS6x's `build.cmd` still builds its libraries alone, with `CPP11` and `ASM6X` set. The compilers find their own headers in their source trees.

`washout.py` (at the repository root) extracts GRIDEd and the eleven tool repositories, whole, into a folder (and with `--zip`, a zip) that builds the same way.

**No RIDE path:** `%APPDATA%\GRIDE\toolchain.json` sets `installation.defaultRoots` to `[]` on this machine only, so GRIDE never looks in `C:\Program Files\RIDE`. The installer will set real paths later; do not hard-code RIDE paths in GRIDEd.

Tools > Toolchain Check should show 23 found, 7 missing (cdb is not installed; Linux and macOS `as`, `cc`, `gdb`, `lldb` cannot exist here).

## Git

- Commit here, on Windows. **Do not push from Windows.**
- The Mac relays: it has remote `windows` pointing at this repo and runs `git relay` (fetch from Windows, fast-forward, push to GitHub).
- To bring GitHub changes here, `git pull` works (the repo is public).
- Line endings: `core.autocrlf true`; sources are LF in git, CRLF on disk.

## Working rules

- Do only what the user asks; ask before changing code beyond it.
- Report results plainly, with what was and was not verified.

## Notes

- GRIDE's dialogs show up in UI Automation as bare panes. To drive them, use Win32 messages (`WM_SETTEXT`, `BM_CLICK`) from a process in the desktop session, and give GRIDE about 15 s after start.
- Known small bug: a file opened after Tools > Convert shows its tab as `Untitled1.shl` while the title bar and Properties name the real file.
