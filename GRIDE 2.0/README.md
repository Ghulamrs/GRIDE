# GRIDE 2.0

A Visual Studio-style IDE for your own toolchain.

GRIDE 2.0 follows RIDE's conventions (the RIDE port, revisions R01-R15, 2026-10; the change log is
`ride-port\GRIDE-RIDE-port.md` beside the port's specification `RIDE-conventions.md`): RIDE's program names and
command lines, read from `toolchain.json` rather than kept in the code; its build rules (a part per compiler, the
TMS6747 program against RTS6x or TI's runtime, the CodeView path of a Windows Debug build); its error shapes; its
runs in the Output pane with an input line; its Compiler Options and settings; Code Composer Studio and Visual
Studio projects read as RIDE and GRIDEd read them; and its debuggers - the C6747's simulator and emulator, cdb, lldb,
a Shalimar program's own session - with RIDE's Debug display and keys. Its example is cppparse, a project of the
one `.pro` format the three outlooks share, built with GRIDE's own cpp11, masm and link (section 2); its Help is the brief page the three outlooks share, with
RIDE 5.17's Complete Manual (GRIDEx3's `docs\`).
Its toolchain is its own: the eleven tools' sources are in `toolchain\`, and `GRIDE.sln` builds them beside `GRIDE.exe`
(section 1).

| Item | Value |
|---|---|
| Languages | C90, C++11, Shalimar |
| Targets | Windows (MASM/LINK), Linux, macOS, TMS6747 DSP (ASM6x, LNK6x, RTS6747, VM6747) |
| Built with | VS 2022, C++/CLI, .NET Framework 4.8, pure WinForms (no third-party libraries) |
| Toolchain | Its own, in `toolchain\`: c90, cpp11, shalimar, c2s, asm6x, lnk6x, rts6x, sim6747, vm6747, masm, link - `GRIDE.sln` builds them, the Shalimar runtimes and RTS6x's libraries beside `GRIDE.exe` |
| Project file | JSON `.pro` |
| IDE settings | JSON, `%APPDATA%\GRIDE\settings.json` |
| CCS 7.4 / 5.5 | Projects read as RIDE reads them: read-only (edited in CCS), built and run as RIDE builds them |
| Visual Studio 2005-2026 | Solutions and C++ projects read as GRIDEd reads them: read-only, built into `.gride\` with GRIDE's toolchain, or with Visual Studio's cl and link.exe when the `.pro` standing for one names `cl` |
| Example | `..\examples\projects\cppparse\cppparse.pro` (GRIDEx3's one examples folder) - open, build, run (section 2) |
| Help | **F1**: the Help the three outlooks share; Language and Toolchain Reference open RIDE 5.17's Complete Manual at its chapters and the Shalimar reference (GRIDEx3's `docs\`, copied beside `GRIDE.exe`) |
| Debugging | sim6747 / vm6747 on the TMS6747, cdb (else lldb) on Windows, a Shalimar program's own session; RIDE's panes and keys (F8, F9, F7, F6) |

---

## 1. Build GRIDE

1. Install **Visual Studio 2022** with:
   - Desktop development with C++
   - **C++/CLI support for v143 build tools**
   - .NET Framework 4.8 targeting pack
2. Open `GRIDE.sln`.
3. Choose **Release | x64** (or Debug | x64).
4. Build > Build Solution. Output: `x64\Release\` - `GRIDE.exe` and beside it the whole toolchain: the ten programs (`c90.exe`, `cpp11.exe`, `shalimar.exe`, `c2s.exe`, `asm6x.exe`, `lnk6x.exe`, `sim6747.exe`, `vm6747.exe`, `masm.exe`, `link.exe`), `toolchain.json`, `docs\` and `lib\` (the Shalimar runtimes `shmrt-x86_64-windows[-debug].lib` and `shmrt-tms6747[-debug]\`, RTS6x's `rts6x-tms6747\` with `rts6x.lib`, `rts6xd.lib`, `printf6x.lib`, `printf6xd.lib`, `shmrt6x.lib`, `shmrt6xd.lib`, and TI's option definitions `ccs\`). GRIDE finds every tool beside itself (**Tools > Check Toolchain** lists them): no RIDE installation, environment variable or Options setting is needed.

**The toolchain in the solution.** `GRIDE.sln` (Visual Studio 2022's format, v143) holds `GRIDE` and the eleven tool projects of `toolchain\`, GRIDE 2.0's own copy of them, in three solution folders: *Compilers* - c90 (`toolchain\Compilers\Compiler-Ci\ide\cc1.vcxproj`), cpp11 (`C++Optimize\ide\cxx1.vcxproj`), shalimar (`Compiler-Si\ide\shc.vcxproj`), c2s (`Converter-C2S\c2s.vcxproj`); *TI* - asm6x, lnk6x, rts6x, sim6747, vm6747 (`toolchain\TI\<tool>\<tool>.vcxproj`) and ar6x (`RTS6x\tools\ar6x`, the archiver rts6x packs its libraries with - a build tool, built into its own folder); *Windows* - masm, link. The build order: GRIDE after the eleven; shalimar after cpp11 (its C6747 runtime is cpp11's output); rts6x after cpp11, asm6x, shalimar and ar6x. Every project builds into the solution's `x64\<Configuration>\`, so the tools land beside `GRIDE.exe`. Nothing runs through `cmd.exe`: shalimar's runtimes (Visual Studio's cl and lib, then cpp11 -S) and RTS6x's libraries (cpp11 -S and asm6x per source, ar6x) are MSBuild tasks in `toolchain\msbuild\Toolchain.targets`. c90 and cpp11 compile in the paths of their own header folders (`toolchain\Compilers\Compiler-Ci\lib`, `C++Optimize\include` and `lib`). `toolchain\MANIFEST.md` says what each tool tree holds and why - minimal copies, taken from `GRIDEdworking` by `toolchain\copy-toolchain.py`.

**Debug | x64** builds the whole solution too, into `x64\Debug` (link's `image.cpp` starts two locals at 0, so its `/WX` no longer stops on C4701); cpp11 warns C4702 (`A64Peep.cpp`), the tool's own, left as it is.

**The toolchain demo**: `examples\toolchain-demo\demo.ps1 [-Configuration Release|Debug] -Outlook "GRIDE 2.0"` - one copy in the examples folder the three outlooks share - runs all eleven programs from `GRIDE 2.0\x64\<Configuration>` (an installation's beside `GRIDE.exe`) as `toolchain.json` spells their command lines, with no `cmd.exe`: `sieve.c` through c90, masm and link on Windows; `shapes.cpp` through cpp11, asm6x and lnk6x against RTS6x on sim6747; `sieve.c` through c2s and shalimar, then run three ways - vm6747 on the assembly, asm6x / lnk6x (`shmrt6x.lib`, RTS6x) on sim6747, masm / link (`shmrt-x86_64-windows.lib`) on Windows - each printing what the C program printed. It writes into `samples\toolchain-demo\out\` only.

The solution also holds, in the solution folder *tests*, `GridePortHarness` - the RIDE port's harness for GRIDE 2.0, a console program compiling `src` in place with its checks (`D:\GRA\My Documents\GIT Test\cpp\sandbox\out\gride-review\ride-port\tests\GridePortHarness\GridePortHarness.vcxproj`, named by absolute path as its own project file names `src`; `ride-port\tests\run-gride-port.ps1` runs it). The harness is built in `Debug | x64` only, after `GRIDE`; where its folder is missing it shows as unavailable, a `Debug | x64` build of the solution stops at it (`MSB3202`, the project file was not found) and the other configurations build `GRIDE` alone.

> The code is compiled and tested: the `GRIDE` project builds in Debug | x64 and Release | x64 with 0 errors and 0 warnings on its own v143 toolset, and the RIDE port's harness (761 checks in fifteen groups, `run-gride-port.ps1 -Test all`) checks every revision with recording tools and with the real tools (RIDE's c90, cpp11, shalimar, masm, link, asm6x, lnk6x, sim6747, vm6747; Visual Studio's cl, clang and link.exe), the CCS and Visual Studio reference projects, and the window itself; the example (cppparse) is built and run in the window, Release and Debug, by the harness and by the run check (`ride-port\tests\run-example.ps1`); the solution builds the whole toolchain in Release | x64 with 0 errors and 0 warnings, Debug | x64 with cpp11's one warning, RIDE's CCS samples Hello, Sample and SampleExt are built and run with it in the window (`ride-port\tests\run-window.ps1`), and the toolchain demo passes in both configurations - last run 2026-10-16.

---

## 2. Try it

1. Run GRIDE.
2. **File > Open Project** (Ctrl+Shift+O) and pick `..\examples\projects\cppparse\cppparse.pro` (installed: `examples\projects\cppparse\cppparse.pro` beside `GRIDE.exe`).
3. You get **cppparse**, the C++ project reader and compiler of the cpp repo: 39 sources and 36 headers in eight groups, which the tree shows as its folders, in the one `.pro` format the three outlooks share (section 6) - its compiler `cpp11`, its include directories and the program's arguments. The same folder holds `cppparse.sln`, for Visual Studio itself.
4. Press **Ctrl+Shift+B** to build - Release or Debug from the toolbar, nothing to set up first under Tools > Options. Every source goes to GRIDE's own `cpp11` (to assembly), then `masm` and `link`; cpp11's headers and library cover what cppparse uses (`<map>`, `<set>`, `<string>`, `<algorithm>`, `windows.h`'s threads and locks, `sys\stat.h`). The link takes the C runtime's libraries from Visual Studio's C++ tools and the Windows SDK, found as `toolchain.json` says - no `vcvars64.bat`, no `cmd.exe`. The program goes to `build\Windows\<Config>` beside the `.pro`.
5. Press **F5** to build and run: `$ .gride\out\Release\cppparse.exe --health .`, cppparse's health report on its own sources, then `[program returned 0]`. The program runs inside the Output pane (section 5, Running the program); **Ctrl+F5** runs it without a build, **Shift+F5** stops it. Its arguments, `--health` and `.`, are Project > Properties > Run > Program arguments; a change there, or another configuration chosen on the toolbar, is written back into `cppparse.pro`.
6. Press **F1** for the Help the three outlooks share (`docs\GRIDE_Help.html`): the outlooks, the example, every outlook's keys, each component's version and where things are. **Help > Language Reference** (C90, C++11, Shalimar) and **Help > Toolchain Reference** (MASM / LINK, ASM6x / LNK6x, RTS6747, VM6747 / SIM6747) open RIDE 5.17's Complete Manual (`docs\RIDE_Manual.html`) at the chapter each is about, the Shalimar language reference and GRIDE's page on the emulator and the simulator. Each page is looked for beside `GRIDE.exe`, in `docs` and `help` under it, then in `..\help` and `..\docs` (an installed layout), as `.pdf`, `.chm`, `.html`, `.htm`, `.md` or `.txt`; the build copies `docs\` beside `GRIDE.exe`.

---

## 3. Folder layout

| File | Phase | Contents |
|---|---|---|
| `GRIDE.sln` | - | The solution: GRIDE, the eleven tools of `toolchain\` (Compilers, TI, Windows) and the port's harness (tests) |
| `src/Main.cpp` | - | Entry point (single translation unit, all classes header-only) |
| `src/Common/Common.h` | - | JSON load/save, paths, text-file encoding, Win32 interop |
| `src/Common/Json.h` | - | JSON as a tree with its keys in order (reads, merges and writes `toolchain.json`) |
| `src/Toolchain/ToolchainFile.h` | - | `toolchain.json`: the shipped file with the user's laid over it, validated; every tool, target and library |
| `src/Settings/IdeSettings.h` | 3 | IDE settings; the tool entries made from `toolchain.json` |
| `src/Settings/CompilerOptions.h` | - | RIDE's Compiler Options: the store (`.pro` / settings), the flags, the availability |
| `toolchain.json` | - | the shipped toolchain definition, copied beside `GRIDE.exe` on every build (`docs/TOOLCHAIN-FORMAT.md`) |
| `src/Project/GrideProject.h` | 4 | `.pro` project model, configurations |
| `src/Editor/Syntax.h` | 5 | Keyword sets, syntax tokenizer |
| `src/Editor/CodeEditor.h` | 5 | Editor control (gutter, colouring, undo, find/replace) |
| `src/Toolchain/Toolchain.h` | 6 | Target info, async process runner |
| `src/Toolchain/VendorTools.h` | - | Visual Studio's tools and `INCLUDE` / `LIB` / `PATH` found without `cmd.exe`; TI's C6000 compiler directory |
| `src/Build/BuildEngine.h` | 7-10 | Build pipeline, error parser, run, convert |
| `src/Build/ProgramRunner.h` | - | A built program run inside the IDE (job object, input line, Stop); what Run / Emulate / Verify run |
| `src/Debug/Debugger.h` | - | The debugger (R09): the engine contract, RIDE's readings of gdb / lldb / cdb, which engine debugs a build, the engine on a pipe or a pseudo-console |
| `src/Debug/ShalimarSession.h`, `DebugProcess.h`, `NativeConsole.h` | - | A Shalimar program's own session; the debugger as a child (pipes, a pseudo-console, a job object) |
| `src/Import/CcsReading.h` | 11 | CCS 7.4 / 5.5 projects read as RIDE reads them: TI's option definitions, the mapping, the refusals, the workspace |
| `src/Import/CcsImporter.h` | 11 | A CCS project as a read-only GRIDE project that builds |
| `src/Import/VsSolution.h` | - | Visual Studio solutions and C++ projects read as GRIDEd reads them (`.sln`, `.vcxproj` with its `.filters`, `.vcproj`) |
| `src/Import/VsImporter.h` | - | A Visual Studio project as a read-only GRIDE project that builds |
| `lib/ccs/*.tsv` | - | TI's option definitions (CCS 7.4's C6000 8.2, CCS 5.5's 7.4), copied beside `GRIDE.exe` on every build |
| `src/UI/Dialogs.h` | 2, 12 | New project, find/replace, properties, input box |
| `src/UI/HelpFiles.h` | - | Help (R13): the Help menu's pages and where they are looked for |
| `src/UI/DebugPanes.h` | - | The debugger's panes: Locals, Watch, Call Stack, Registers, Memory, Breakpoints (RIDE's Debug display) |
| `src/UI/CompilerOptionsEditor.h` | - | RIDE's Compiler Options dialog (a panel), Project > Compiler Options |
| `src/UI/OptionsDialog.h` | 3, 6 | Tools > Options (with GRIDE Paths and Compiler Options) |
| `src/UI/MainForm.h` | 2, 4, 7-12 | Main window |
| `..\docs\` | - | Help's pages, GRIDEx3's and shared by the three outlooks, copied beside `GRIDE.exe` on every build: `GRIDE_Help.html` (the one brief page), `RIDE_Manual.html` (RIDE 5.17's Complete Manual), `Shalimar_Reference.html`, the chapter pages `C90_`, `Cpp11_`, `MASM_LINK_`, `ASM6x_LNK6x_`, `RTS6747_` and `VM6747_Reference.html`; `TOOLCHAIN-FORMAT.md` |
| `toolchain/Compilers/`, `TI/`, `Windows/` | - | The toolchain's sources, one tree per tool with its project file (section 1): Compiler-Ci (c90), C++Optimize (cpp11), Compiler-Si (shalimar), Converter-C2S (c2s); ASM6x, LNK6x, RTS6x (with `tools\ar6x`), SIM6747, VM6747; MASM, LINK |
| `toolchain/msbuild/Toolchain.targets` | - | The tools' build steps as MSBuild tasks: shalimar's runtimes, RTS6x's libraries |
| `toolchain/MANIFEST.md`, `copy-toolchain.py` | - | What each tool tree holds and why; the copy from `GRIDEdworking` (it keeps the tree's own `shc.vcxproj`, `rts6x.vcxproj`, `ar6x.vcxproj`) |
| `..\examples\toolchain-demo\` | - | `demo.ps1`, `sieve.c`, `shapes.cpp` (one copy for the three outlooks): the eleven programs run from `x64\<Config>` with no `cmd.exe` (section 1) |
| `../examples/` | - | GRIDEx3's one examples folder, the three outlooks' and their Setup's: `projects\` - cppparse (section 2), RIDE's C, C++ and Shalimar projects, the CCS samples - and `programs\` |

---

## 4. First-time setup (Tools > Options > Toolchains)

After a build of `GRIDE.sln` every tool is beside `GRIDE.exe` and nothing needs setting up here (section 1). Every tool the IDE runs comes from **`toolchain.json`** beside `GRIDE.exe`: RIDE's program names, argument conventions, the libraries and where each thing is looked for (the format: `docs\TOOLCHAIN-FORMAT.md`). The IDE keeps none of it in its code; a renamed program or a changed switch is a change to the file. The user's copy (`%APPDATA%\GRIDE\toolchain.json`, or `toolchain.user.json` beside the exe) overrides it field by field. Tools > Options > **Definitions** shows both files, the problems the reader found (a field of the wrong kind is dropped with a message naming the file, the entry and the field, and the shipped value stands; a missing shipped file leaves every entry empty), the file's tools and libraries with the overridden ones marked, and offers *Edit user overrides...* (makes the file with its header and opens it), *Reload* (which looks for the user's file again, so a `toolchain.user.json` put beside the exe while GRIDE runs is read) and *Discard user overrides*. Tools > **Check Toolchain** prints the same to the Output pane with where every tool was found or looked for; the problems are printed there at start-up too.

Tools > Options > **Toolchains** lists one entry per thing the IDE runs, each made from the file (the Definition column names the tool and operation it follows): a program, an argument template, include / define / library switches, an error regex, environment, a setup script and an output code page. An entry follows the file until you change its program, arguments or switches, which marks it *changed here*; *Restore from toolchain.json* takes the file's definition back. A name without a path is looked for as RIDE looks for it: the tool's environment variable (`$C90`, `$CPP11`, `$SHALIMAR`, `$ASM6X`, `$VM6747`, `$SIM6747`, `$C2S`, `$RTS6X`), beside `GRIDE.exe`, the toolchain's installation (**Tools > Options > General > Toolchain installation** - its root or its bin; else the file's default roots, `Program Files\RIDE`, holding one of its markers, `c90.exe` or `cpp11.exe`, in their bin), then PATH where the file allows it (not for RIDE's `link` and `lnk6x`, which Microsoft's and TI's would be taken for). The other names the file lists (`cc1.exe`, `cxx1.exe`, `shc.exe`, `ASM6x.exe`) are aliases tried after RIDE's.

| Entry | From the shipped file | Notes |
|---|---|---|
| c90 / cpp11 / shalimar | `c90.exe`, `cpp11.exe`, `shalimar.exe`: `-S "$(Source)" -o "$(AsmFile)" $(ArchFlag) $(ConfigFlags) ...` | `-arch <arch>` / `--target=<arch>`; shalimar takes no `-I` and no `-D`; `$(ConfigFlags)` is the tool's per configuration (`-g -D_DEBUG=1` / `-O2 -DNDEBUG=1`; shalimar's `--debug -g`, `-g` on the C6747) |
| cl (Windows) | `cl.exe /nologo /c ... $(CompileFlags) /Fo"$(ObjFile)" "$(Source)"` | Microsoft's compiler, the machine's own, found in Visual Studio's folders; a Visual Studio project whose `.pro` names `cl` (the example) compiles with it, the project's own ClCompile settings through `projectSettings`, and links its objects with Microsoft's `link.exe` |
| masm / link | `masm.exe -t x64 "$(AsmFile)" -o "$(ObjFile)"`; `link.exe /nologo /subsystem:console /out:"$(Output)" $(Objects) ... $(CrtLibs)` | a Release build's; the CRT list is the file's `hostCrt` library, which RIDE's link finds through `LIB` - GRIDE sets it from Visual Studio's folders (below) |
| clang / link.exe (Windows Debug) | `clang.exe -c "$(AsmFile)" -o "$(ObjFile)"`; Microsoft's `link.exe /nologo /subsystem:console /DEBUG ...` | a Debug build's CodeView path: the compilers write the GNU spelling (`-masm=gnu`), clang assembles it, link.exe writes the PDB; looked for in Visual Studio's folders (`vcvars`) |
| ml64 / TI's lnk6x | `ml64.exe /nologo /c /Fo"$(ObjFile)" "$(AsmFile)"`; TI's `lnk6x.exe` (with lnk6x's arguments) | the vendor's tools the native question offers (section 5); TI's is looked for only in TI's C6000 compiler directory (`C:\ti\...`), never in RIDE's bin |
| asm6x / lnk6x | `asm6x.exe "$(AsmFile)" -o "$(ObjFile)"`; `lnk6x.exe -mv6740 --abi=eabi -i "$(RuntimeDir)" "$(LinkerCmd)" ... $(Objects) $(LibFlags) -o "$(Output)"` | a project without a `.cmd` gets the file's `ti-link.cmd` (RIDE's) written into `obj` |
| RTS6x | `rts6x.lib` in `lib\rts6x-tms6747` (the file's `rts6x` library) | a Debug build takes `rts6xd.lib` beside it |
| sim6747 / vm6747 | `sim6747.exe --run -c "$(Program)"` runs the `.out`; `vm6747.exe "$(Program)" $(ShalimarRuntimeDir) $(Arguments)` runs the assembly | |
| as / cc (Linux, macOS) | those machines' | these targets only reach assembly here |
| c2s | `c2s.exe --to-shalimar "$(Source)" -o "$(Output)"` / `--to-c ...` | C -> Shalimar and Shalimar -> C |
| cdb / lldb (Windows) | `cdb.exe -y "$(OutDir)" "$(Program)"`; `lldb.exe "$(Program)"` | the debugger of a Windows Debug build and the one standing in where cdb is not installed (`$GRIDE_CDB`, `$GRIDE_LLDB`, PATH); driven through their `session` in the file |

**Placeholders** (Help > Placeholder Reference): the file's `$(Source) $(AsmFile) $(ObjFile) $(Objects) $(Output) $(Program) $(Arch) $(ArchFlag) $(ConfigFlags) $(IncludeFlags) $(DefineFlags) $(LibFlags) $(CrtLibs) $(CompileFlags) $(LinkFlags) $(RuntimeDir) $(LinkerCmd) $(Arguments) ...`, each explained in its `placeholders` section, and GRIDE's own `{in} {out} {objs} {libs} {crtlibs} {includes} {defines} {configflags} {flags} {asmflags} {linkflags} {arch} {config} {name} {projectdir} {outdir} {objdir} {asmdir} {exe} {map} {cmdfiles} {runtime} {runtimedir} {args}`; both are filled in. A configuration is Debug unless its name says Release.

**The project's compiler** (Project > Properties > Toolchain): `auto` gives each file its own compiler by extension; `c90`, `cpp11` or `shalimar` (`cc1`, `cxx1`, `shc` are the same words) take every file, and refuse one they do not compile with RIDE's sentence.

**Compiler Options** (RIDE's dialog; **Project > Compiler Options**, and Tools > Options > **Compiler Options** for what a new project starts with): one tab per `toolchain.json` `compilerOptions` tab - General (`-nologo`, `-j`, `-time`), C (c90), C++ (cpp11), Shalimar - with the configuration edited chosen above (Debug or Release), an option that means nothing for the project's target greyed with RIDE's reason as its tip (`--no_compress` is the C6747's, shalimar's `--debug` is not), the command line the tab's compiler gets shown below, and *Restore defaults*. c90, cpp11 and shalimar take their `$(ConfigFlags)` from it: the defaults are RIDE's (`-g -D_DEBUG=1` / `-O2 -DNDEBUG=1`; `--debug -g`, `-g` on the C6747), and only what differs is written into the `.pro`. *Report what the optimizer declined* sets `CPP11_DECLINES=1` for cpp11's runs. A CCS project's options are its configurations' (the menu shows its mapping).

**GRIDE Paths** (Tools > Options; White's `settings.json` keys, kept in GRIDE's): the installation's header directories (`include` for cpp11, `lib` for c90; relative to the toolchain's installation or absolute, empty for the directory of that name there), `vcvars64.bat` (names the Visual Studio whose cl, ml64, link.exe and clang are used - its folder is taken, the batch file never run - before vswhere's newest), TI's C6000 compiler directory and a TI library directory (named, a C6747 build links against TI's runtime - `rts6740_elf_eh.lib` where it is, else `rts6740_elf.lib` - instead of RTS6x; with none named the newest one under `C:\ti` is used when RTS6x is not there, and refused with RIDE's sentence when it holds only `rts6740_elf.lib`; TI's `lnk6x` stands in for a missing RIDE `lnk6x`, said in the Output pane, with `--rom_model` where the project gives no model), the include directories every compile searches after the project's and the libraries every host link takes, the compiler a new project starts with, the C6747 debugger (White's choice, sim6747 or vm6747; `GRIDE_DEBUG_SIM6747=1` / `GRIDE_DEBUG_VM6747=1` at start win), whether CCS projects open, and the CCS installation `${CG_TOOL_ROOT}` is found under (else TI's compiler directory).

**Shalimar**: set its file extensions and keywords in Options > Languages (defaults: `.shl`, placeholder keywords).

---

## 5. How a build works

| Step | Tool | Output |
|---|---|---|
| 1. Compile (`.c`, `.cpp`, Shalimar) | Compiler for the file's extension | Target assembly in `obj\` |
| 2. Assemble (`.asm`, `.s` or step 1 output) | Target assembler | Object file |
| 3. Link | Target linker + libraries (+ RTS6747, `.cmd` files for TMS) | `.exe` / `.out` / binary |

- **Incremental:** only changed files rebuild. A change to a project header, compile settings or tools rebuilds all; linker changes force a relink.
- Headers reached only through include paths are not tracked: use **Rebuild**.
- Output folder (default): `build\$(Target)\$(Config)`.
- Errors from MSVC/MASM, TI (asm6x/lnk6x/compiler) and GCC formats appear in the **Error List**; double-click to jump.

RIDE's rules (ride-port's `RIDE-conventions.md`, 4.3-4.7), every name and argument from `toolchain.json`:

- **A part per compiler.** The files one compiler takes are compiled together, the compilers in the order they first appear in the project, each part announced as `$ <project> (c90)`. A **Shalimar program** of several `.shl` files is one `shalimar` command with them all (`$(Sources)`), its assembly and object named after the program; Compile File takes one `.shl` alone.
- **The environment the compilers read** (RIDE's `prepareFor`): each compiler's `*_AS` / `*_LD` (`C90_AS`, `C90_LD` ...) name the target's assembler and linker, empty for a Debug build carrying CodeView. On Windows every tool runs with **Visual Studio's `INCLUDE`, `LIB` and `PATH`**, found without `cmd.exe` (vswhere, else the file's default roots; the newest MSVC, its clang, the newest Windows SDK) unless the tool has a setup script of its own. The installation's header folders follow the project's include folders (`lib` for c90, `include` for cpp11).
- **Windows**: a Release build assembles with RIDE's masm and links with RIDE's link (`$ linking with link`, `[built <program>]`); a **Debug** build adds `-masm=gnu`, assembles with clang and links with Microsoft's `link.exe /DEBUG` (the PDB) - with a warning, and masm / link, when they are not there.
- **TMS6747** (RIDE's `makeTiProgram`): asm6x, then `$ lnk6x N objects, rts6xd.lib[, c6747.cmd] -o prog.out` and `[linked <program>]`. The runtime is RTS6x of the configuration (`rts6xd.lib` for Debug); a project's `libc.a`, `rts6740*`, `rts6747.lib`, `rts6x.lib` are read as it. A Shalimar program links `shmrt6x[d].lib` before it, or, where that library is not packed, the Shalimar runtime's assembly (`lib\shmrt-tms6747[-debug]`, `$SHMRT6747[D]`) assembled into `obj\shmrt`. The project's `.cmd` files, else the file's `ti-link.cmd` written into `obj`. Failures: `asm6x refused the assembly - ...`, `lnk6x did not link it - see its messages above`. **Emulate only** (Project > Properties; `emulateOnly` in the `.pro`): the build stops at the assembly, which vm6747 runs - `[no .out: the project says emulateOnly - Run is Emulate on vm6747]`.
- **A Shalimar program on Windows** links the target's Shalimar runtime first (`shmrt-x86_64-windows[-debug].lib` from RIDE's `lib`, `[... linked first: the Shalimar runtime]`); the Debug one carries the program's own debugging session.
- **Linux, macOS**: the sources reach assembly here and nothing assembles or links them: `x86_64-linux only reaches -S here - switch to x86_64-windows to run it`; a run of them says the same.
- **A folder that cannot be written to** is refused before anything runs: `<dir> cannot be written to, and the program would go there - copy the project to a folder of your own, such as Documents, and build it there`.
- **The native question** (RIDE's `askNative`; Tools > Options > General, on by default): when the project's own assembler or linker failed (on the TMS6747, lnk6x), the compilers named no fault in the source and nothing names an unresolved or duplicate symbol, GRIDE asks *The project's own masm and link did not build it. Use Visual Studio's ml64 and link.exe for this build instead?* (or TI's lnk6x). Yes builds again with them and says `[built with ..., not GRIDE's ...]`; no leaves the build as it failed.

### Debugging

**F8** builds the Debug configuration and starts the debugger RIDE would choose for it (`src\Debug\`, GRIDEd's engines): on the TMS6747 the simulator on the `.out` when it is this build's and carries the program's line table, else the emulator on the assembly (Tools > Options > GRIDE Paths > *TMS6747 debugger* picks one; `GRIDE_DEBUG_SIM6747=1` / `GRIDE_DEBUG_VM6747=1` at start win); on Windows cdb on the PDB (`$GRIDE_CDB`, PATH; on a pseudo-console), else lldb (`$GRIDE_LLDB`), else - for a Shalimar program - the program's own session (the Debug Shalimar runtime is linked first); Linux and macOS programs are debugged on their own machines. A Release configuration is refused before anything is built, with RIDE's sentence. **F9** (or a click in the gutter left of the line numbers) sets a breakpoint, a red dot; the stop is a yellow arrow in the gutter, and the engine's lines go to the Output pane (`[debug] stopped at stepped.c:14 in main`), the program's own lines with them. The panes beside Output and Error List are RIDE's Debug display: **Locals** (the stop line on top, `Name | Value | Type | Address`, a value changed since the last stop red and bold, the engine's reason in brackets when it has none - the C6747's emulator and simulator have no locals, a Shalimar program says where it is, not what is in it - White's keys along the bottom, and a line where `name = value` sets a variable), **Watch** (expressions read again at every stop and frame change, kept between sessions; the line under the list adds one, a double-click or Enter edits one, an empty answer or Delete removes it; a refusal grey; `add a watch...` last), **Call Stack** (`Function | File | Line`, the frame looked at bold; a double-click looks at another), **Registers**, **Memory** (an address, 256 bytes), **Breakpoints** (double-click goes there, Delete removes). **F7** steps over, **F6** steps into, Debug > Step Out and Run to Cursor; **Ctrl+Up** / **Ctrl+Down** walk the stack (`main.c:11 in main - where the call came from`, `back where it stopped`); Debug > Watch Expression and Set Variable; **F8** carries on; Shift+F5 or Debug > Stop Debugging ends the session. At the end: `the program ran to the end and returned 3` and `F8 starts it again. The breakpoints are still where you put them.`

### Running the program

**F5** builds and runs, **Ctrl+F5** runs without a build, **Shift+F5** stops - the build, or the running program and everything it started (a job object). The program runs inside the IDE, as in RIDE's window: `$ <program> <arguments>`, its output in the Output pane, an **input line** under it (Enter hands a line to the program and shows it; Ctrl+Z ends its input), then `[program returned N]` or `[stopped]`. The program's arguments are Project > Properties > Run > Program arguments (one command line; `programArgs` in the `.pro`, each argument whole), quoted one by one when it starts. Tools > Options > General > *Run programs in the Output pane* off gives the way before the port, a console window of its own (Windows programs).

- **TMS6747**: Run is the simulator on the `.out` - `$ sim6747 --run prog.out` (`sim6747 --run -c`, the program's status, and its `CYCLES count=...` line); it takes no command line (`[run] the simulator takes no command line - Emulate on vm6747 hands them over`). Without a simulator or an `.out`, RIDE's sentences say why. A project that says *Emulate only* runs on the emulator. **Debug > Emulate on vm6747** builds and runs the build's assembly (`$ vm6747 build\...\obj`: the program, the Shalimar runtime's assembly for a Shalimar program, then the arguments). **Debug > Verify** runs both, captured, and compares them line for line, the `CYCLES` line aside: `[verify] the two agree, line for line`, or the first line they differ at.
- **Linux, macOS**: a run is turned away before anything is built, with RIDE's sentence.

### CCS projects

**File > Open** a CCS 7.4 or 5.5 project's `.project`, `.ccsproject` or `.cproject`, or **File > Import CCS Project** on its folder (a workspace folder is not a project: RIDE's sentence names its projects). GRIDE reads the three files as White does - TI's own option definitions (`lib\ccs`, named by `toolchain.json`'s `ccs` section) for what CCS left unstored - and maps each configuration onto RIDE's toolchain: the optimisation (`-O3` is `-O2`), `-g`, the defines and undefines (a define holding a space, a tab or a quote is handed over quoted whole, as RIDE's Compiler Options hand one over: `-D"GREETING=hi there"`), the include paths, `--no_compress` (cpp11), the heap, the stack, `--rom_model` / `--ram_model`, the libraries (`libc.a` and TI's runtimes are RTS6x), the library search paths (one not on this machine left out and said), the project's `.cmd`, the files each configuration excludes, linked resources (`PARENT-n-PROJECT_LOC`, `WORKSPACE_LOC`, path variables) and build variables. An option RIDE does not support, or decides itself (`-mv6740`, the diagnostics, the map file), is named, never dropped: the Output pane shows RIDE's lines at open (`CCS project K6747c (Release): 1 option not supported, using RIDE's defaults: --opt_for_speed=5; RIDE's own instead of: ...`), the tree each configuration's mapping, **Project > CCS Mapping** RIDE's read-only dialog. A project RIDE refuses (not a C674x, COFF, big-endian) still opens, with the refusal as a note. The project is read-only - its options are CCS's - but it builds and runs: Debug or Release from the toolbar, the program in the temporary directory (`gride-ccs-<project>`), never in the CCS folder. What RIDE keeps for it itself - the configuration chosen, the program's arguments and *emulateOnly* (Project > Properties > Run) - is kept in GRIDE's `settings.json` under the project's folder (`CcsProjects`), never in CCS's files. Tools > Options > GRIDE Paths > *Open CCS projects* off turns the CCS menu items and the opening off.

### Visual Studio solutions and projects

**File > Open Project** a `.sln`, `.vcxproj` or `.vcproj` (Visual Studio 2005 to 2026). GRIDE reads it as GRIDEd does (cppparse's reader and what a build needs beyond it): the solution's projects, folders, dependencies and configurations; a project's configurations with their conditions, its items (wildcards expanded), its `.filters`, the files excluded per configuration, `$(ProjectDir)` / `$(SolutionDir)` / `$(Configuration)` ... GRIDE holds one project: a solution with one C++ project opens it, one with several asks which. The project opens read-only for the Windows target - Debug and Release from the file's own (`x64` before `Win32`) with their defines, undefines, include directories, libraries and library directories, `Optimization` as `$(Optimise)` - and builds with GRIDE's toolchain (RIDE's compilers on Compiler Options' defaults) into `.gride\out\<configuration>` beside the project; the Visual Studio files are never written. The tree shows the `.filters`' folders (else the directories) and each configuration's mapping, the title `[Visual Studio 2022 v143, read-only]`, the Output pane what was read and what is not mapped (other configurations and platforms, build events, custom builds, property sheets, C++/CLI, cl's own options). **Project > CCS / VS Mapping** shows the reading. A project opened itself is read with the solution that lists it (its folder or up to three above), so `$(SolutionDir)` resolves; the configuration chosen and the program's arguments are kept in GRIDE's settings under the project file.

**A `.pro` that stands for a Visual Studio project** (R12): `"visualStudio"` names the `.sln` or `.vcxproj` beside it, and the `.pro` keeps what GRIDE keeps for that project - the compiler word (`toolchain`), the configuration chosen, the program's arguments, `emulateOnly` - where a project opened directly keeps them in the settings. Everything else is read from Visual Studio's files at every open and build, as above, and they are never written; the `.pro` is written only when one of its words changes (a configuration chosen, Project > Properties), and keeps the compiler word as written. Its compiler word `cl` (Project > Properties > Build > Toolchain; `msvc` is the same) builds the project with **Visual Studio's cl and link.exe** instead of RIDE's compilers: every C and C++ source goes to `cl` (`$ <project> (cl)`), which writes the object itself, with the configuration's flags, then the switches the project's own ClCompile settings map to through `toolchain.json`'s `projectSettings` (`[the project's own settings give cl: ...]`; a setting it does not name is listed, `[not given to cl - ...]`), then the project's `AdditionalOptions`; the installation's header directories are not given to cl (they would stand in front of Visual Studio's). The objects are linked by Microsoft's `link.exe` with no CRT list - cl's objects name their own C runtime (`/MT`, `/MTd` ...). `cl` builds for the machine it is on: a project for the TMS6747 is refused with `cl has no tms6747 target`.

---

## 6. `.pro` file format

**One format for the three outlooks** (2026-10-20): White's, which is RIDE's. A project saved here opens in Black and
White, and theirs open here; each writes back the others' keys as it read them, and RIDE's `//` comments are read.
`..\examples\projects\cppparse\cppparse.pro`, shortened:

```json
{
  "name": "cppparse",
  "toolchain": "cpp11",
  "arch": "x86_64-windows",
  "include": [ "cppparse/driver", "cppparse/common", "..." ],
  "groups": {
    "driver": [ "cppparse/driver/main.cpp" ],
    "...": [ ],
    "headers": [ "cppparse/analyzer/Ast.hpp", "..." ]
  },
  "build": { "target": "cppparse", "groups": [ "driver", "...", "report" ], "args": [ "--health", "." ] }
}
```

- `toolchain`: the project's compiler word (`auto` when absent: each file's language chooses).
- `arch`: the platform built for, `x86_64-windows` or `tms6747`; another machine's host (`arm64-darwin`, `x86_64-linux`) is this one's, and is written back as it was.
- `groups`: the files, relative to the `.pro`, in named groups - the tree's folders, in this order. A group may be `{ "files": [ ... ], "toolchain": "c90" }`, with its own compiler. A file added goes to the group of a file of its kind in its folder, else `Sources` or `Headers`.
- `include`, `libraries`: header directories and libraries for every configuration.
- `build`: `target`, the program's name; `groups`, the groups it is built from (a source in another group is not built, and is grey in the tree); `args`, the program's arguments for a run, each one whole. A `.pro` with no `build` builds its `Sources` group.
- `emulateOnly`: `true` for a TMS6747 project whose build stops at the assembly (written only when true).
- `options`: the project's Compiler Options, `{ "debug": { "c90.opt": "-O1" }, "release": { ... } }` - the option ids of `toolchain.json`'s `compilerOptions`, only the values that differ from the defaults (written only when there are some).
- GRIDE 2.0's own, each written only when it says something: `language` (`C90` | `C++11` | `Shalimar`; from the files when absent), `buildConfigurations` (a list, when they are not Debug with `_DEBUG` and Release with `NDEBUG`; `configurations` is GRIDEd's, an object, and is kept as read) and `activeConfiguration`.

A `.pro` of GRIDE 2.0's form before the one format (`formatVersion`, `sourceFiles`, `includePaths`, `target`, `outputName`, `programArgs`) is read as it was and written in the one format at its next save. **A `.pro` standing for a Visual Studio project** keeps that form - `formatVersion`, `name`, `activeConfiguration`, `toolchain`, `emulateOnly`, `programArgs` and `visualStudio`, the `.sln` or `.vcxproj` it stands for (section 5, Visual Studio):

```json
{
  "formatVersion": 2,
  "name": "cppparse",
  "activeConfiguration": "Release",
  "toolchain": "cl",
  "programArgs": [ "--health", "." ],
  "visualStudio": "cppparse.sln"
}
```

---

## 7. Keyboard shortcuts

| Keys | Action |
|---|---|
| Ctrl+Shift+N / Ctrl+N | New project / new file |
| Ctrl+Shift+O / Ctrl+O | Open project / open file |
| Ctrl+S / Ctrl+Shift+S | Save / save all |
| Ctrl+F / Ctrl+H / F3 / Ctrl+G | Find / replace / find next / go to line |
| Ctrl+Shift+F | Find in files |
| Tab / Shift+Tab | Indent / outdent selection |
| Ctrl+Shift+B / Ctrl+F7 | Build project / compile current file |
| F5 / Ctrl+F5 / Shift+F5 | Build and run / run / stop (the build, the program or the debugger) |
| F8 | Start debugging (the Debug configuration is built first) / carry on when stopped |
| F9 | Toggle a breakpoint on the caret's line (or click the editor's gutter, left of the numbers) |
| F7 / F6 | Step over / step into (Debug > Step Out has no key, as in RIDE) |
| Ctrl+Up / Ctrl+Down | While stopped: look at the frame that called this one / come back down |
| Ctrl+Tab | Next document |
| Alt+F7 / F4 | Project properties / properties window |
| F1 | Help: the page the three outlooks share |

---

## 8. Changing things

| To change | Where |
|---|---|
| A program's name, an argument, a switch, a configuration flag, a library | `toolchain.json` - the user's copy (Tools > Options > Definitions > *Edit user overrides...*), field by field; `docs\TOOLCHAIN-FORMAT.md` lists every field and has a "Changing the toolchain" list |
| How a debugger is driven (its commands, preamble, marker, a console) | the tool's `session` in `toolchain.json` (`like` another tool's to start from it) |
| A Compiler Options row | `toolchain.json`'s `compilerOptions`, and the compiler's `options` list |
| Where the toolchain's installation, Visual Studio, TI's compiler, CCS are | Tools > Options > General and GRIDE Paths (`%APPDATA%\GRIDE\settings.json`, or `settings.json` beside the exe) |
| The build rules | `src/Build/BuildEngine.h` (parts, links, the TMS6747 program, the refusals) |
| The runs | `src/Build/ProgramRunner.h` (`ProgramLauncher::Plan`, `ProgramRunner`) |
| Which debugger debugs a build | `src/Debug/Debugger.h` (`DebuggerChoice::For`); the readings of each debugger's answers: `DebugTranscript` |
| The debug window | `src/UI/DebugPanes.h`, `src/UI/MainForm.h` (the Debug menu, `StartDebugging`, `LookAtFrame`) |
| The example's compiler, configuration, arguments | `..\examples\projects\cppparse\cppparse.pro` (Project > Properties, the toolbar); cl's switches for a Visual Studio project's own settings: `projectSettings` of `cl` in `toolchain.json` |
| A Help page, or which item opens it | `docs\` (copied beside `GRIDE.exe` on every build: `CopyHelpFiles` in `GRIDE.vcxproj`); the items and the folders searched: `src/UI/HelpFiles.h` |
| What is read from CCS / Visual Studio | `src/Import/CcsReading.h`, `src/Import/VsSolution.h`; the mapping onto a project: `CcsImporter.h`, `VsImporter.h` |

---

## 9. Known limits

- RIDE's CCS sample SampleExt, built in Release, prints wrong output on sim6747 (its labels, or its numbers, go missing; the program returns 0): the same `-O2` assembly runs right on vm6747, its Debug build is right, and RIDE 5.17's own cpp11 and rts6x do the same - a fault in the toolchain's C6747 chain (asm6x, lnk6x, rts6x, sim6747), not in GRIDE.
- The tool list is fixed (one entry per thing the IDE runs, each made from `toolchain.json`); programs and arguments are fully editable. The four targets are the IDE's own: a fifth target in the file is read but has no place in the IDE.
- A program in the Output pane sees pipes, not a console (RIDE's window gives it a pseudo-console): one that asks whether it has a console behaves as under a pipe. The C6747 simulator takes no arguments.
- With a setup script, arguments pass through `cmd`: avoid `& | < > ^` in paths and flags.
- A CCS project is edited in CCS, not in GRIDE: its options are read afresh at every open and build; its pre- and post-build steps are shown, not run; a project it depends on is not built.
- A Visual Studio project is edited in Visual Studio: with RIDE's compilers it builds only as far as their headers reach (cl's own settings are shown, not given) - a `.pro` naming `cl` builds it with Visual Studio's cl and link.exe and the project's own settings; property sheets are not evaluated, build events and custom build steps not run, a project it depends on not built, a static library or DLL linked as a program.
- The breakpoints last as long as the window: they are not saved, and they stay on their line numbers while the file is edited.
- Error List has no filter buttons yet.
