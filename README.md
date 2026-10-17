# GRIDEx2

Three IDEs for RIDE's toolchain, sharing one copy of that toolchain:

| Folder | What it is |
|---|---|
| `GRIDEd\` | GRIDEd - C++/CLI, .NET Framework 4.8, multi-file; `GRIDE.sln` builds GRIDE and the eleven tools into `x64\<Config>\` |
| `GRIDE 2.0\` | GRIDE 2.0 - C++/CLI, .NET Framework 4.8, single translation unit; the same build into its own `x64\<Config>\` |
| `GRIDE 1.0\` | RIDE 5.17, the White outlook (*RIDE - The Simplest*), seal `ride-5.17.dat`; `RIDE.sln` builds RIDE (window and console) and the eleven tools from `..\toolchain` (`build.bat solution`, into `bin\`). RIDE's examples (`programs\`, `projects\`) and help (`help\`) are its own, as each IDE keeps its own. Not built in GRIDE1 yet; the installer's White option is still to be added on Windows. |
| `toolchain\` | the eleven tools the three solutions build (`..\toolchain\...`): c90, cpp11, shalimar, c2s (`Compilers\`), asm6x, lnk6x, rts6x, sim6747, vm6747 (`TI\`), masm, link (`Windows\`) |

The folders `My Documents\GRIDEd` and `My Documents\GRIDE` are copies of this tree's closed state, each with the toolchain
inside (`toolchain\` instead of `..\toolchain\`). Build one of the two solutions at a time: their tool projects
share the intermediate folders under `toolchain\` (situation W7 below).

## Installers

| Folder | What it makes | State |
|---|---|---|
| `installer\windows\` | `GRIDE-Setup-<ver>.msi`: one Setup for both IDEs, written through Windows Installer's own `msi.dll` (no WiX, no Inno Setup). What the user sees names only **GRIDE** and its two outlooks: screen 1 welcomes; screen 2 chooses the **Black outlook** (GRIDEd) or the **Grey outlook** (GRIDE 2.0) like a skin, and installs it with the eleven tools, `toolchain.json`, the runtime libraries, the VC++ 2022 runtime and `examples\cppparse` beside it, into `GRIDE\Black` or `GRIDE\Grey`, with one Start menu entry, *GRIDE*. Repair / Remove and Programs and Features (*GRIDE 1.0.0*) name no outlook. A quiet install takes `IDE=Black` (the default) or `IDE=Grey`; `MSIINSTALLPERUSER=1` installs per user without elevation. | Built and checked on 2026-10-16 (1.0.0, 9.9 MB): Microsoft's ICE suite 104 of 104, 0 errors, 0 warnings; `test-setup.ps1` installs each outlook per user, finds every file, the shortcut and the registry values, starts GRIDE.exe and cpp11, repairs a deleted program and uninstalls cleanly - all checks pass for Black and for Grey; a keyboard click-through (Enter, Alt+G, Alt+I) installs Grey only. |
| `installer\macos\` | `build-pkg.sh`: one macOS product (`productbuild`) with two choices, the editor app (RIDE's `macos\Window.xcodeproj`, kept in `macos\`) and the shared toolchain. GRIDEd and GRIDE 2.0 are Windows-only, so the Mac package carries the editor, not them. | Written on Windows and checked only with `bash -n` - never run. A Mac build also needs the tools' Makefiles, which this toolchain copy leaves out (`installer\macos\README.md`). |

Build the MSI: build both solutions in `Release | x64` (a plain Build, not a Rebuild of one after the other - W7),
then `installer\windows\build-setup.ps1`, `validate-msi.ps1 -Msi ... -OutDir installer\dist` and `test-setup.ps1`.
Output goes to `installer\dist\`, which git ignores.

## Keeping GRIDEx2 working on Windows and the TMS targets

What GRIDEx2 depends on outside its own tree, what goes wrong when that is missing or different, and how to fix
it. This is a reference for later work: nothing here is required by an installer today, and the fixes are not yet
made unless the Status column says so.

### What it needs today

| Part | Needs on the machine |
|---|---|
| Building the solutions | Visual Studio 2022 or later with *Desktop development with C++*, *MSVC v143 - VS 2022 C++ x64/x86 build tools*, *C++/CLI support*, the .NET Framework 4.8 targeting pack and a Windows 10/11 SDK |
| `GRIDE.exe` (Release) | .NET Framework 4.8, and the VC++ 2015-2022 runtime: `VCRUNTIME140.dll` (both IDEs) and `MSVCP140.dll` (GRIDEd), which the installer ships beside the exe |
| The eleven tools (Release) | Windows only (static CRT, no other DLL) |
| Building for TMS320C6747 | nothing more: c90 / cpp11 / shalimar, asm6x, lnk6x, rts6x, sim6747 and vm6747 are all the toolchain's own |
| Building for Windows | Visual Studio's MSVC libraries (`libcmt`, `libvcruntime`, `legacy_stdio_definitions`), the Windows SDK's (`libucrt`, `kernel32`) and, for a Debug build's line table, *C++ Clang tools* and Microsoft's `link.exe` |
| Debugging Windows programs | `cdb` (the Windows SDK's *Debugging Tools for Windows*) |

The TMS320C6747 target therefore keeps working on any Windows machine with the Release build; the Windows target
is the one that leans on Microsoft's tools.

### Windows target

| # | Situation | What breaks | Solution | Depends on | Status |
|---|---|---|---|---|---|
| W1 | Windows SDK present, no Visual Studio of any kind (not even Build Tools) | No Windows program links: RIDE's `link` needs `libcmt`, `libvcruntime` and `legacy_stdio_definitions`, which only Visual Studio has; and GRIDE puts the SDK's own library folders on `LIB` only when it finds a Visual Studio | **W1a** find the SDK on its own and always pass `ucrt\x64` and `um\x64` on `LIB`. **W1b** a small Windows runtime of the toolchain's own (below) so C programs link against Windows' built-in C runtime | W1a: `toolchain.json`'s `visualStudio.windowsKits`, `GRIDEd\GRIDE\src\Services\VisualStudio.h`, `GRIDE 2.0\src\Toolchain\VendorTools.h`. W1b: W1a; Windows 10 or later (`ucrtbase.dll`); the SDK's `ucrt.lib` and `kernel32.lib`; a new library project under `toolchain\`; a `toolchain.json` library entry; GRIDE choosing it when the MSVC libraries are absent | open |
| W1b | (the runtime) C programs | Linked with only the SDK's `ucrt.lib` + `kernel32.lib`, the demo's `sieve.c` misses exactly two symbols: `mainCRTStartup` and `printf` (the UCRT's `printf` is an inline in the headers, not an export). Linked with the static `libucrt.lib` instead, 25 are missing (`memcpy`, `__chkstk`, `__security_cookie`, `__C_specific_handler` ...) | A `crt0` library: `mainCRTStartup` (argc/argv from the UCRT, static initialisers, `exit(main(...))`) and the `printf` family as functions over `__stdio_common_vfprintf`; built by the toolchain's own c90 and masm (or by MSVC with `/Zl /GS-`, so it names no Microsoft library) | as W1 | open |
| W1c | C++ programs (cpp11) | `shapes.cpp` misses seven: `mainCRTStartup`, `printf`, `operator delete`, `_purecall`, and cpp11's exception and RTTI calls in Microsoft's ABI: `_CxxThrowException`, `__CxxFrameHandler3`, `type_info`'s vftable | W1b plus `operator new` / `delete` and `_purecall` in the same library, and an import library for `vcruntime140.dll` (the exception and RTTI entries) made by the toolchain from a `.def` | W1b; `vcruntime140.dll` on the machine running the program (the VC++ Redistributable, or a copy beside the program); a way to make import libraries (RIDE's `link` cannot yet - to check) | open |
| W1d | Shalimar programs for Windows | `shmrt-x86_64-windows.lib` is compiled by MSVC and asks for `LIBCMT`, `libcpmt` and `OLDNAMES` (`/DEFAULTLIB`), so it cannot link without Visual Studio | Build the Shalimar runtime with `/Zl` and without the C++ standard library (or with the toolchain's own compilers) so it links against W1b | W1b; `toolchain\msbuild\Toolchain.targets`, the shalimar runtime sources | open |
| W1e | No Windows SDK either | Even W1b has no `ucrt.lib` or `kernel32.lib` | The toolchain makes its own import libraries for `ucrtbase.dll` / the `api-ms-win-crt-*` sets and `kernel32.dll` from `.def` files | W1c's import-library maker | open |
| W2 | Visual Studio without *C++ Clang tools* | A Windows Debug build has no line table: GRIDE goes on with `masm` / `link` and says so, and the debugger sees no source lines | Install the component (`Microsoft.VisualStudio.Component.VC.Llvm.Clang`); later, `masm` writing CodeView line records itself | the component; or the masm sources in `toolchain\Windows\MASM` | documented |
| W3 | No `cdb` (the SDK installed without *Debugging Tools for Windows*) | Windows programs cannot be debugged; GRIDE falls back to its placeholder, which walks the source text and runs nothing | **a** modify the installed Windows SDK (Settings > Apps > *Windows Software Development Kit* > Modify > Change > *Debugging Tools for Windows*); **b** WinDbg (`winget install Microsoft.WinDbg`) with `RIDE_CDB` naming its `cdb.exe`; **c** a cdb-compatible console of the toolchain's own over `C:\Windows\System32\dbgeng.dll`, which every Windows has: the commands GRIDE sends (`bp`, `g`, `p`, `t`, `gu`, `dv /t /V`, `k`, `??`, `r`, `db`, `.printf`) are all carried out inside that DLL | a, b: GRIDEd looks in `Windows Kits\10\Debuggers\x64` (`cdb`'s `lookup.dirs`) and at `RIDE_CDB`; GRIDE 2.0 only at `RIDE_CDB` and `PATH` until W13 is done. c: the SDK's `DbgEng.h` / `dbgeng.lib` to build it; the cdb `session` in `toolchain.json` unchanged | a/b available (GRIDE 2.0: with `RIDE_CDB`); c open |
| W4 | Running the `Debug` output on another machine | `GRIDE.exe`, cpp11, sim6747 and link built in Debug need Visual Studio's debug runtime (`ucrtbased`, `VCRUNTIME140D`, `MSVCP140D`), which may not be redistributed | Ship and copy only `x64\Release` | none | documented |
| W5 | Copying `x64\Release` to a machine without the VC++ runtime | `GRIDE.exe` does not start (`VCRUNTIME140.dll`, `MSVCP140.dll`) | The VC++ 2015-2022 Redistributable (x64), or the runtime DLLs beside `GRIDE.exe` as the installer does | the redistributable | documented |
| W6 | A tool's environment variable is set (`C90`, `CPP11`, `SHALIMAR`, `C2S`, `ASM6X`, `VM6747`, `SIM6747`, `RIDE_CDB` ...) | It names the program outright, before the tool beside `GRIDE.exe`, so an older tool (say a RIDE 5.17 cpp11 without the `T (&)[N]` fix) runs instead of the solution's own. RIDE's installer sets none of them; a user or a script may | Tools > Check Toolchain already shows where each tool came from; add a warning when a variable points away from `x64\<Config>` | `toolchain.json`'s `lookup` order; the Check Toolchain dialog of each IDE | open |
| W7 | Both solutions building at once - or a Rebuild of one after the other | The tool projects keep their objects in `$(ProjectDir)build\<Config>\obj\` inside the shared `toolchain\`, so two builds write the same files. Seen on 2026-10-16: a Rebuild of GRIDE 2.0 right after one of GRIDEd deleted GRIDEd's ten programs from `GRIDEd\x64\Release` (the Clean step follows the shared build logs); a plain Build of GRIDEd put them back | Intermediate folders under each solution: `$(SolutionDir)x64\$(Configuration)\toolchain-obj\$(ProjectName)\`, and the same for `Toolchain.targets`' runtime steps | every tool `.vcxproj` under `toolchain\`, `toolchain\msbuild\Toolchain.targets`; the old folders' copies too | open (build one at a time) |
| W10 | `cmd.exe` blocked by policy | Nothing in the solutions' builds uses it. A *setup script* (vcvars) under Tools > Options does, and `cpp11 -c` / c90 without `-S` shell out through `std::system`; GRIDE's own pipeline uses `-S` and runs each step itself | Keep the `-S` pipeline; leave setup scripts empty | none | documented |
| W11 | The pinned MSVC version (`visualStudio.msvcVersion`) is not installed | GRIDE takes the newest and says so | - | `toolchain.json` | done |
| W12 | Windows on ARM64 | The x64 tools run under emulation; `visualStudio.hostBin` names `bin\Hostx64\x64`, which an ARM64 Visual Studio may lack | Choose the host folder by the machine's architecture | `toolchain.json`, VisualStudio.h / VendorTools.h | open |
| W13 | The two IDEs' `toolchain.json` drift apart | Today they differ in one field (descriptions aside): `tools.cdb.lookup.dirs`, which GRIDEd has and GRIDE 2.0 neither has nor reads, so GRIDE 2.0 does not find an SDK-installed `cdb` by itself | Give GRIDE 2.0 the field and its reader (`lookup.dirs` searched like GRIDEd's `RideToolchain` does), then keep one shipped `toolchain.json` content for both and check it on every commit | `GRIDE 2.0\toolchain.json`, `GRIDE 2.0\src\Toolchain\ToolchainFile.h` and its lookup; a comparison step in the commit routine | open |

### TMS320C6747 target

| # | Situation | What breaks | Solution | Depends on | Status |
|---|---|---|---|---|---|
| T1 | SampleExt (RIDE's CCS sample) in Release on sim6747 | Recorded in GRIDE 2.0's R15: its labels go missing while the numbers print; RIDE 5.17's own cpp11 and rts6x give the same wrong output with the project's link options, and the same `-O2` assembly runs right on vm6747. A run on 2026-10-15 with other link options printed all 34 lines right | Find the fault in the C6747 chain: run the same `.s` on vm6747 and the linked `.out` on sim6747 with a trace, compare the sections `lnk6x` lays out (`.const`, `.cinit`, the stack and heap from the project) with the link options that work | asm6x, lnk6x, rts6x and sim6747 sources under `toolchain\TI\`; SampleExt's `.cproject` | open |
| T2 | A CCS project keeps CCS's default 2 KB stack (`0x800`) | C++ Debug programs that print doubles overflow it: Sample needs `0x1000`, SampleExt `0x2000`; RIDE's own default is `0x4000` | Warn in the Output pane when a C++ Debug link gets less than `0x2000` from the project, naming RIDE's default; the project's own value stays the one used | each IDE's CCS reader and build engine (heap and stack from `.cproject`) | open |
| T3 | A CCS project's linked file is not on this machine (K6747c needs `C:\ti\ccsref\src\util.c`) | GRIDEd says so only when it builds (GRIDE 2.0 when it opens) | Report it when the project opens, in both | GRIDEd's CCS reader | open |
| T4 | TI's own C6000 tools (`C:\ti`) are absent | Nothing: the toolchain's asm6x, lnk6x and rts6x build and sim6747 runs the program; TI's directory under RIDE Paths is optional | - | - | done |
| T5 | A Debug build without a line table, or for a Release build | sim6747 cannot debug it by line; GRIDE takes vm6747 on the assembly, or says why nothing can debug it | - | `-g` from the Compiler Options | done |

### Order of work, when it is taken up

1. **W7** - separate intermediate folders, so both solutions can be built and tested together.
2. **W1a**, then **W1b**, **W1d**, **W1c** - Windows programs without Visual Studio, C first.
3. **W13**, then **W3c** - the same `cdb` lookup in both IDEs, then Windows debugging with no download.
4. **T1**, **T2**, **T3** - the C6747 chain's fault and the CCS warnings.
5. **W6**, **W12**, **W1e** - the rest.

Each change goes in here first, is built with Visual Studio (`devenv.com GRIDE.sln /Rebuild`) for both
solutions one after the other, then committed and synced into the old folders.
