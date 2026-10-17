# Windows installers (Inno Setup)

`setup.exe` installer for RIDE 4.7, built on the Windows box with Inno Setup 6
(`winget install JRSoftware.InnoSetup`). The 3.x releases are sealed and are
packaged from their own tree, not this one.

RIDE 4.0 ships the project's own x86-64 assembler `masm` beside the editor,
which the installed `settings.json` names in place of `ml64`
(`"assembler": "bin/masm.exe"`), and the project's own x86-64 linker
`link.exe` beside it (LINK) and the C6000 linker `lnk6x.exe` (LNK6x), both
named too (`"linker": "bin/link.exe"`, `"tilinker": "bin/lnk6x.exe"`).
`"askNative": true`: when one of ours fails a build and the source is not at
fault, the editor asks whether to use the vendor's tools for that build (found
by vswhere / the `ti` directory, never PATH); false never asks. It also carries
a license-safe TI build path (`bin\ti\ti-build.cmd`) that produces a real C674x
`.out`/`.hex` with a TI CGT install found on the machine (no TI binaries are
shipped).

## How to build (on the box)

1. Build the workspace so the binaries exist: `tools/to-windows.sh` (into
   `C:\Users\GRA\source\RIDE\bin`; it carries `..\MASM`, `..\LINK` and
   `..\LNK6x` too).
2. Stage the install tree: `stage.cmd <RIDE> <C++Optimize> <stage40>`
   (sources from `bin\`; ships masm.exe and names it), then copy
   `EXPRESS-HELP-4.7.md` to the stage root as `EXPRESS-HELP.md`, and
   `ti-build.cmd`+`ti-link.cmd`+`TI-BUILD.txt` into `stage40\bin\ti\`.
3. `mkinstaller.cmd RIDE-4.7.iss` → `RIDE-4.7-setup.exe`.

## One-shot build scripts

`build-installer.bat` (Windows) and `build-installer.sh` (Linux/macOS) do the
whole job in one command: compile every compiler project and the RIDE editor,
regenerate the HTML docs, stage the tree, and produce the installer.

    build-installer.bat 4.7        REM -> dist\RIDE-4.7-setup.exe (Inno Setup)
    ./build-installer.sh 4.7       #   -> dist/RIDE-4.5-<os>.tar.gz (no Inno on Unix)

Both take the version as `%1`/`$1` (default 4.5) and honour `CPP` (the C++ clone
with `include/` and `lib/`) and `OUT` (output dir) as overrides. The `.iss` take
`/DStage=` and `/DOutDir=` so the stage/output paths are not hard-coded. HTML is
regenerated with `docs2html.py` when Python is present; otherwise the committed
`help/manual.html`, `help/guide.html` and `EXPRESS-HELP-<ver>.html` are used.

The `.iss` files name box-local stage paths (`C:\Users\GRA\ride-pkg\...`);
adjust the `Stage` define for another machine.

## Layout the installer lays down

    <install>\bin\     RIDE.exe, RIDEConsole.exe, the compilers, vm6747, asm6x, c2s, masm, link, lnk6x
    <install>\bin\lib\ Shalimar runtime (.lib) and shmrt-tms6747\*.s
    <install>\bin\ti\  ti-build.cmd - real-silicon TI build path
    <install>\include\ C++ headers      <install>\lib\ C headers
    <install>\examples\  <install>\help\  <install>\docs\  EXPRESS-HELP.md
