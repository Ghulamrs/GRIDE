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

```
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" GRIDEd\GRIDE\GRIDE.vcxproj -p:Configuration=Release -p:Platform=x64 -p:SolutionDir=C:\Users\GRA\work\GRIDEd\GRIDEd\ -m
```

Output: `GRIDEd\bin\x64\Release\GRIDE.exe`, with `toolchain.json`, `docs` and `lib\ccs` beside it. Close GRIDE before rebuilding. It must build with 0 warnings.

## Development tools (no installer yet)

GRIDE finds each tool by `toolchain.json`'s lookup: environment variable, tool folder, **beside GRIDE.exe**, RIDE install, PATH. During development the tools sit beside `GRIDE.exe`. Staging is manual for now:

1. Build each tool project Release|x64; output is its own `x64\Release` (`Compiler-Si` lives in `ide\x64\Release`, `Compiler-Ci` and `C++Optimize` likewise under `ide\`).
2. Build `Compilers\Compiler-Si\ide\shc.sln`, not `shc.vcxproj`: its post-build needs `cpp11.exe` beside `shalimar.exe` to build the Shalimar runtime.
3. Build RTS6x with `TI\RTS6x\build.cmd`, with `CPP11` and `ASM6X` set to the built `cpp11.exe` and `asm6x.exe`. It writes `TI\RTS6x\build\*.lib`.
4. Copy the ten exes (c90, cpp11, shalimar, c2s, masm, link, asm6x, lnk6x, sim6747, vm6747) into `GRIDEd\bin\x64\Release`; RTS6x's four libs into `lib\rts6x-tms6747\` there; `Compilers\Compiler-Si\ide\x64\Release\lib\*` into `lib\` there.

Rebuilding a tool does not refresh the staged copy. The compilers find their own headers in their source trees.

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
