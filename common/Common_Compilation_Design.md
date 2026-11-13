# Common compilation for the three outlooks - design document

Fable 5.2, 2026-11-12. **Draft 4: the decisions of section 11 are taken (discussed with the user the same day), the common build log is added (5.1, D14), and two requirements of the user's follow, both decided: files by pattern (3.4, D15), and the two files - `.pro` the one common project format, `.grws` kept as Black's workspace file (3.5, D16).
Steps 0 (the one `toolchain.json`), 1 (the census) and 2 (the block, `compile.dll`, with `gride-plan.exe` and its tests - `common\compile\`) 3 (Black builds through the block; the dry run, 5.2 and D17, asked for by the user on 2026-11-13) and 4 (Grey builds through the block) are implemented; White does not use the block yet.**

Written after `GRIDE common command block - study - Opus 5.5.md` (Documents folder), whose proposal this
document accepts in the main and turns into a design: what the one model is, how a project's tools are chosen,
where every setting lives, what each outlook's pages become, and in what order it is built. Section 11 records
what was decided. Two decisions changed draft 1: the block is a **DLL, `compile.dll`**, not a static library, and
it **exports its C++ classes** rather than plain C functions (section 7).

The study's claims were checked against the code at main `f698bc2` before this was written. The commit the study
names, `d21f4be`, is not in the repository; its findings match `f698bc2`, with the corrections of section 10.

## 1. The aim

Today Black, Grey and White each read a project and its settings in their own code, choose the tools in their own
way and write the command lines in their own way: about 22,000 lines in three code bases, which already disagree
(section 2). The aim is **one compilation model**:

1. **One description of the tools** - a single shipped `toolchain.json`, the same file for the three.
2. **One vocabulary of settings** - the build settings of `settings.json` under the same names for the three.
3. **One project format in full** - the `.pro`, including its configurations, read and written alike.
4. **One rule for choosing the tools of a project** - written once, and explained to the user on request.
5. **One piece of code that turns 1-4 into commands** - one native DLL, `compile.dll`, the same file beside each
   `GRIDE.exe` (called "the block" below), which *plans* and never
   *runs*.
6. **The same pages in the three outlooks** for the settings above, each drawn in the outlook's own style.

What stays each outlook's own: its window, editor, keys, menus' arrangement, Output pane, process runner, debugger
session, and Black's workspaces.

Not in scope: the compilers and tools themselves; the debugger sessions' dialogue; macOS (its copy is a fourth
code base and is not built here); Black's and Grey's Visual Studio import beyond what section 9 says.

## 2. Where the three stand today

Checked in the code; the lines are in section 10's sources.

| | Black | Grey | White |
|---|---|---|---|
| Tool definitions | `toolchain.json` + the user's file | its own copy of `toolchain.json` + the user's file | none: names and options in code |
| Where a user changes a tool | settings `toolchains.<target>` (10 templates per target) | settings `Tools[]` (path, arguments, switches) | Tools menu items: assembler, linker, TI linker |
| Build settings in `settings.json` | under `environment.*` (camelCase) | top level, PascalCase | top level, lower case |
| Configurations in `.pro` | `configurations` keyed `"Debug\|Windows"` | `buildConfigurations` | none |
| Windows pipeline | `-S`, masm, link | `-S`, masm, link | `-c` (the compiler runs masm), then link |
| Program and objects | `out\<Config>\<Win\|C6747>`, `obj\...` | `build\<Target>\<Config>`, `obj` under it | `<root>\<name>.exe` for Debug and Release alike; objects in `%TEMP%` |
| Rebuilds only what changed | yes | yes, plus a stamp | no: every build is a full build |
| Command form | one string, re-split | one string | one string with `cd X && ...`, re-split |
| Compiler for a source | group, else project, else by extension | extension, overridden by group, else project; a mismatch refused | group, else project or the settings' default, else by extension |

Three facts decide the shape of the solution:

- **The same three steps everywhere.** For a C++ project on Windows all three end at
  `cpp11 ... -g -D_DEBUG=1 -masm=masm`, then masm, then GRIDE's `link ... /DEBUG` with GRIDE's runtime; on the
  C6747 at cpp11 `-S -arch tms6747`, asm6x, lnk6x with `ti-link.cmd` and `rts6x.lib`. The differences are in the
  spelling, the folders and the leftovers - not in what is built. A common planner is therefore possible without
  changing what a build *is*.
- **The data is nearly common already.** `toolchain.json`'s format describes every tool the three run. Black and
  Grey read it with two readers (1,363 and 1,378 lines, 501 lines apart); White reads it not at all.
- **The drift is real and has cost already.** The two shipped `toolchain.json` differ in four places, so Grey links
  the Release runtime into a Debug build. The same define arrives twice (`-D_DEBUG=1 ... -DDEBUG` in Black). One
  setting has three names (`environment.windowsRuntime`, `WindowsRuntime`, `windowsRuntime`), so a choice made in
  one outlook is not seen by another, although the Help says the three share one settings file.

## 3. The model

```
        common\toolchain.json  (shipped, one file)      %APPDATA%\GRIDE\toolchain.json (the user's changes)
                         \                                   /
                          +---- Definitions (merged) -------+          what tools exist, how each is called
   %APPDATA%\GRIDE\settings.json  "build": { ... }                      this installation's choices
   <project>.pro                                                        this project's files and choices
                                   |
                        compile.dll  (native, one file)   Workspace::load(...)   then   plan(Request)
                                   |
        Plan = steps { program, argv[], directory, environment, inputs, outputs, stamp, why }
                                   |
     Black's runner        Grey's runner        White's runner          each runs the steps and shows the output
```

**The block plans; the outlook runs.** Given a project, a configuration (Debug, Release), a target
(`x86_64-windows`, `tms6747`, ...) and an action (build, rebuild, clean, compile one file, run, emulate,
simulate, debug, convert), the block returns the ordered steps. It starts no process, opens no window and keeps
no state between calls.

### 3.1 One `toolchain.json`

One file in the repository, `common\toolchain.json`, copied beside each `GRIDE.exe` by its project and installed
by Setup. Black's and Grey's copies go; White ships it from the day it reads it (step 5), not before - a file
nothing reads would only mislead. Its content is Black's, with Grey's one deliberate difference kept (section 10,
item G1).

**Done, step 0, 2026-11-12**: the file is `common\toolchain.json`; `GRIDEd\GRIDE\toolchain.json` and
`GRIDE 2.0\toolchain.json` are gone and both projects copy the one file. Against Black's old copy it changes one
line (cpp11 is no longer handed its own header folders, which made it warn on `<limits>` in Black as it once did
in Grey - checked by running cpp11 both ways: the same assembly, the warnings gone). Against Grey's old copy it
changes four: the Debug runtime's libraries and their names, gride-dbg's step filter, grt's description.

**Measured with the census** (`common\compile\census\after-step0`, the 228 builds of Black and Grey recorded again
with the new file and set against the baseline as sets of commands):

- **Grey: 27 link commands changed and nothing else** - every Debug build for Windows that reaches its link now
  names `grtstartd`, `grtcompatd`, `grtcxxd`, `grtcd` and `grtwind.lib` where it named the five Release ones. The
  28th, `compilerpp`, stops before its link (section 10, C1). No compile, assemble or C6747 command differs.
- **Black: no command changed in any of the 114.** The line taken from Grey turned out to have no effect on
  Black's builds on this machine: with these settings Black hands none of a compiler's install folders to it, before
  or after (no `-I<install>\...` on any of its 306 compile commands, for cpp11 or c90). The warning on `<limits>` was
  produced by giving cpp11 those folders by hand; no recorded build of Black gave them. The line is right to take all
  the same - a machine whose settings name the folders would have passed them - but step 0 fixed nothing a user of
  Black saw here.

Everything a build needs to know about a tool is in this file, and **every key of it is read**. The names that
are still in code in all three move into it: `/NODEFAULTLIB` and its list, `/DEBUG`, `/subsystem:console`,
`/stack:8388608`, `-mv6740 --abi=eabi`, `--rom_model`, the runtime's layer names, the folder names `obj` and
`shmrt`, the source and header extensions. New fields the design needs:

| field | on | meaning |
|---|---|---|
| `responseFile` | a tool | how the tool takes a file of arguments (`"@$(File)"`); used when a line passes 30,000 characters |
| `languages`, `headerExtensions` | top level | a language's source extensions and the role of its compiler; the files never compiled. Replaces three lists in code. (Draft 1 put `sourceExtensions` on each compiler; a language's extensions are one fact whoever compiles it, so it is one list at the top.) |
| `folders`; `folder` on a target | top level | the output and object folder patterns (section 5), and the target's name in them (`Win`, `C6747`) |
| `linkFlags` | a linker | flags by configuration: what is `/DEBUG` in code today. `/subsystem:console` was already in the link's template. White's `/stack:8388608` (finding C5) is not taken: only White gave it, and only to C++. |
| `refuses` on a library; `refuseLibraryFlag` on a target | | Microsoft's runtime names that a link with GRIDE's runtime must not take, and how the linker is told (`/NODEFAULTLIB:`) - a list in Black's and Grey's code today |

These fields are in `common\toolchain.json` since step 2 and are described in `docs\TOOLCHAIN-FORMAT.md`.

A user's changes go into the user's `toolchain.json` and nowhere else (decision D5). The merge rule stays as it
is documented: objects merge field by field, anything else replaces the shipped value.

### 3.2 One vocabulary in `settings.json`

A new object, **`"build"`**, holds every setting that affects what a build does. Its keys take White's short
names, because Grey's own page already labels them so. Editor, layout, recent files and key sets stay where each
outlook keeps them.

| `build.` key | meaning | Black today | Grey today | White today |
|---|---|---|---|---|
| `root` | an installation with a `bin` folder, where one is used (added in step 2: Black and Grey have the key) | `environment.grideRoot` | `GrideRoot` | `root` |
| `include`, `lib` | the installation's header and library folders | `environment.grideIncludeDir`, `grideLibDir` | `GrideIncludeDir`, `GrideLibDir` | `include`, `lib` |
| `includes`, `libraries` | include folders and libraries every project gets | `environment.sharedIncludes`, `sharedLibraries` | `SharedIncludes`, `SharedLibraries` | `includes`, `libraries` |
| `compiler` | the compiler when a project says `auto` or names none | `environment.defaultCompiler` | `DefaultCompiler` | `compiler` |
| `windowsRuntime` | GRIDE's runtime, Microsoft's, or automatic | `environment.windowsRuntime` | `WindowsRuntime` | `windowsRuntime` |
| `windowsDebugger`, `c6747Debugger` | which debugger | `environment.*` | `WindowsDebugger`, `C6747Debugger` | `windowsDebugger` |
| `vcvars`, `ti`, `tilib` | Visual Studio and TI's compiler, where used | `environment.vcvars`, `tiCompilerDir`, `tiLibDir` | `Vcvars`, `TiCompilerDir`, `TiLibDir` | `vcvars`, `ti`, `tilib` |
| `jobs` | files compiled at once | `environment.buildJobs` | `BuildJobs` | `buildJobs` |
| `askNative` | ask before falling back to Microsoft's or TI's tools | `environment.askNativeTools` | `AskNativeTools` | `askNative` |
| `options` | the Compiler Options' defaults for new projects | `compilerOptions` | `CompilerOptions` | `options` |
| `showCommands` | whether the Output pane shows each command line (the log file always does) | `environment.showCommandLines` | always shown | not shown |
| `paths` | named folders a `.pro` may use | `paths` (read only) | `paths` | `paths` (read only) |
| `ccs` | `{ enabled, root }` | `environment.*` | `CcsEnabled`, `CcsRoot` | `ccs` |

**Moving to it without losing anything.** On load an outlook reads `build.<key>`; where it is absent it reads its
old key once and writes the value under `build`. For one release the old keys are still written beside the new
ones, so that an older GRIDE opened afterwards still finds its settings; the release after drops them. (Setup's
removal clears the settings in any case; this matters for a portable copy and for the release in between.)

White's `assembler`, `linker` and `tilinker`, Black's `toolchains.<target>` and Grey's `Tools[]` do not get a
`build` key: they are changes to a tool, and go to the user's `toolchain.json` (3.1). Each is read once, written
there, and no longer read.

### 3.3 One `.pro`

The core keys are already common and stay: `name`, `toolchain`, `arch`, `groups` (a list, or
`{ files, toolchain }`), `include`, `libraries`, `paths`, `build { target, groups, args }`, `emulateOnly`,
`options { debug, release }`, `open`. Added to the common format:

- **`configurations`**, Black's form, keyed `"Debug|Windows"`: `outDir`, `objDir`, `includeDirs`, `libDirs`,
  `libs`, `linkerCmd`, `compileFlags`, `linkFlags`, `outputName`. Grey's `buildConfigurations` is read and written
  back as `configurations`; White keeps what it finds and, for the first time, honours it.
- **`excludeFromBuild`** (Black's) for all three.
- **Defines leave the configurations.** `-D` comes from the Compiler Options only (`options.debug.*.defines`), so
  one define is given once. A `defines` list found in an old configuration is moved into the options on load.

An unknown key is kept as read, as the three readers do today.

### 3.4 Files by pattern, not by name (decision D15)

**Today** every file of a project is written out by name in the three outlooks: `compilerpp.pro` spends 33 of its
48 lines on 16 `.cpp` and 16 `.h` that are simply everything in its folder. No reader knows a `*`. The list goes
stale the moment a file is added outside GRIDE, and two people adding files collide on it.

**The rule:** an entry of a group is a **pattern**; a plain name is the pattern that matches one file, so every
`.pro` written until now stays valid and means what it meant.

```json
"groups": {
  "Sources": ["*.cpp", "backend/*.cpp"],
  "Headers": ["*.h", "*.hpp", "backend/*.h"],
  "Input":   ["sample.cpp"]
},
"build": { "target": "compilerpp", "groups": ["Sources"] }
```

| | |
|---|---|
| **What a pattern is** | `*` (any run of characters) and `?` (one character) in the **file-name part only**. The folder part is written out, relative to the `.pro`, with `/`: `backend/*.cpp`. A folder is not searched below itself; each folder that holds sources has its own entry. |
| **A name beats a pattern** | `sample.cpp` above is named in `Input`, so `*.cpp` in `Sources` does not take it. Between two patterns the first group in the file wins. A file is in one group only. |
| **Order** | A group's files are its entries in the order written; the files of one pattern are sorted by name without regard to case. The order is therefore the same in the three outlooks and on Windows and macOS, and the link order with it. |
| **Where the order matters** | Shalimar compiles a program's files in one run, and the first is the program's own. Such a group names that file first and the pattern after it - `"Sources": ["primes.shl", "*.shl"]`: the name beats the pattern, so it is first, and the others follow by name. An outlook writes it so by itself (step 2, finding P9). |
| **`exclude`** | A project-level list of names or patterns that are in no group at all: `"exclude": ["scratch*.cpp", "old/legacy.cpp"]`. This is what "Remove from project" writes for a file a pattern covers. `excludeFromBuild` stays what it is: in the project, shown, not built. |
| **Never matched** | The project's own `out` and `obj` folders (and a configuration's `outDir` / `objDir`), whatever a pattern says; the `.pro`, `.build.log` and stamp files. |
| **When it is resolved** | When the project is opened, when its window comes forward, and always when a build is planned. A file put in the folder by any other program is in the next build without the `.pro` being touched. |
| **Nothing matches** | A pattern with no file is not an error: one line in the build log (`Sources: backend/*.cpp matches no file`). A **named** file that is gone is the error it is today. A build group with no source at all is the error it is today. |
| **A file that went away** | Its object is no longer in the plan, so it is not linked; the plan's file list is part of the link step's stamp, so the program is linked again. Clean removes the orphan object. |
| **Matching of case** | As the extensions of section 4: without regard to case, on both hosts, so one `.pro` gives one list everywhere. |

**What an outlook writes**

- **A new project**: patterns for its language - `"Sources": ["*.cpp"]`, `"Headers": ["*.h", "*.hpp"]` (C: `*.c`
  and `*.h`; Shalimar: `*.shl`). No file names.
- **Add New File / Add Existing File**: if a pattern of the group already covers the file, nothing is written. If
  the file is in a folder of the project that no entry covers, the pattern of that folder is written
  (`backend/*.cpp`) and the outlook says in one line how many files came with it. A file outside the project's
  folder (`../shared/util.cpp`) is written by name.
- **Remove from project**: a named entry is taken out; a file covered by a pattern goes into `exclude`.
- **An existing `.pro` that lists names** (decided): when a group's named files in a folder are exactly the files
  of that kind there, the names are written back as the one pattern on the next save, because nothing is lost by
  it at that moment; Output says so in one line. Otherwise the names stay. No outlook writes a pattern, and the
  29 example projects are not converted, before all three read patterns - that is, with step 5 (section 9).

**Where it is done:** in the block (`Project::files()`), once for the three, with the operating system's own
directory calls - C++11 has no `<filesystem>`. The outlooks ask the block for the files of a group; none of them
expands a pattern itself. The build log's head says what each pattern gave (section 5.1).

Projects read from Visual Studio's or CCS's own files are not touched by any of this; their files are whatever
those files say.

### 3.5 Two files: the project, `.pro`, and the workspace, `.grws` (decision D16)

**Decided:** there are two kinds of file, and each keeps its extension.

| file | what it is | who reads and writes it |
|---|---|---|
| `<name>.pro` | **the project**: its files (by pattern, 3.4), its tools and options, its configurations | the three outlooks, through the block; one format |
| `<name>.grws` | **the workspace**: a list of projects opened together, with the startup project, the active configuration and target, the breakpoints, and what GRIDE keeps for a project read from Visual Studio or CCS | Black, as today; its format does not change |

The question was put because `.grws` had never been agreed. Draft 4 first proposed to fold the list of projects
into the `.pro`; the user's rule was that a second extension is justified only where there really are two files,
and there are: a project, and a list of projects, are different things, and Black has both today. So `.grws` is
ratified as it stands, and only the `.pro` is made common.

- **Nothing of the workspace goes into the `.pro`**, and nothing of the project into the `.grws`. The one overlap
  today is the active configuration - `activeConfig` in the `.grws` (Black), `activeConfiguration` in the `.pro`
  (Grey) - and it stays where each outlook keeps it: a workspace has one for all its projects, a single project
  has its own.
- **A single project needs no `.grws`.** Opened in Black from its `.pro` it gets a workspace in memory, as now;
  the file is written only when the user saves the workspace.
- **Grey and White** open one `.pro`, as now, and are not taught the `.grws` by this design. Should they be one
  day, the format is already there and Black's reader is the one to move into the block.
- **The build order of a workspace** (`dependsOn`) stays Black's: it asks the block for the plan of each project
  in that order, and the log of each project is the same text as when the project is built alone (D14).
- The `.grws` reader stays in Black for now and moves into the block with the CCS and Visual Studio readers
  (step 8), so that `gride-plan.exe` can plan a workspace as well.

## 4. Choosing the tools of a project

One rule, in this order, the same in the three. Each step of a plan carries a line saying which rule chose its
tool, so the outlook can show *why* (section 6, the Build Plan page).

1. **The target** is the one asked for (the outlook's active target). A `.pro` that names an `arch` builds only
   for that arch: asked for another, the plan has no steps and one message, in all three. (White today rewrites a
   foreign arch to the host's on load, and builds a Windows program when Linux is chosen.)
2. **The language of a source** is its extension, compared without regard to case on Windows, from
   `toolchain.json`'s lists: `.c` C; `.cpp .cc .cxx .c++` C++; `.shl .shm` Shalimar; `.s .asm` assembly; headers
   are never compiled. (Grey loses `.c++` and sends `.C` to c90 through its own lower-casing; this is one list.)
3. **The compiler of a source**: the group's `toolchain` word; else the project's; else `build.compiler`; else
   `auto`. `auto` means the compiler whose role is that language's on the target (`cCompiler`, `cppCompiler`,
   `shalimarCompiler`). A named compiler that does not take the source's language is refused with a message
   naming the file, the compiler and where the name came from - except a compiler that takes several languages
   (`cl`), as Grey does now.
4. **Assembler and linker** are the target's roles. A Debug build on Windows asks the compiler for its own
   CodeView (`asmSyntaxFlags.ownCodeView`) and stays on GRIDE's masm and link; only a compiler without it goes to
   `debugAssembler` / `debugLinker`.
5. **The runtime and libraries**: the target's `runtime` entry in the configuration's files (Debug names for a
   Debug build - which Grey's copy lacks today), then every library whose `when` holds: `always`; `shalimar`
   when a source is Shalimar; `printf` when the project names `printf6x`. `build.windowsRuntime` chooses between
   the `crt: grt` and `crt: msvc` entries.
6. **Fallbacks** are the roles `nativeAssembler` / `nativeLinker`. The plan says that the question applies
   (`asksNative`); the outlook asks it, as now, and asks again for a plan with the answer.
7. **Where a tool is**: `lookup.env`, the installation's `bin`, `lookup.dirs`, beside `GRIDE.exe`, `PATH`,
   Visual Studio's folders - in the order the tool's `lookup` gives. A tool not found stays in the plan under its
   name with a problem saying every place looked in.

## 5. What a build writes, and where

One rule, from `toolchain.json`'s new `folders` entry (decision D3):

| | pattern | example |
|---|---|---|
| program | `out\<Config>\<Target>\<name><ext>` | `out\Debug\Win\c-bank.exe`, `out\Release\C6747\c-bank.out` |
| objects, assembly, the build log, `ti-link.cmd`, response files | `obj\<Config>\<Target>\` | `obj\Debug\Win\main.obj` |
| an object's name | the source's base name when unique in the project, else made from its path | `account.obj`; `src_a_util.obj`, `src_b_util.obj` |

A configuration's `outDir` / `objDir` in the `.pro` overrides the pattern for that project.

**The pipeline is the same in the three** (decision D2): the compiler writes assembly (`-S`), the target's
assembler makes the object, the target's linker the program. White leaves `-c` and stops setting `C90_AS` /
`CPP11_AS` for the compiler to run masm itself.

**A step's environment is its own, and small** (settled in step 2 from findings C7 and E of section 10). A step
carries only what its tool reads:

- a Compiler Option that sets a variable (`CPP11_DECLINES`) sets it when it is on, and is absent when it is off;
- a tool that `toolchain.json` documents as reading `INCLUDE`, `LIB` or `PATH` (cl, GRIDE's link for Microsoft's
  runtime) is given Visual Studio's folders there, in front of what the variable already holds;
- nothing else. The compilers' `C90_AS` / `C90_LD` (and the `CPP11_`, `SHALIMAR_` pairs) are not set: with `-S`
  no compiler starts an assembler or a linker. `INCLUDE` and `LIB` are not given to GRIDE's compilers or to masm:
  none of them reads either (checked in their sources). The literal `%INCLUDE%` at the end of Black's and Grey's
  values is gone with them.

**Up to date**: each step has a stamp - its program's path, its argv and the environment it is given, hashed. A
step runs when its stamp changed or an input is newer than an output. Headers are the project's header files, as
today. This gives White incremental builds, and makes a changed Compiler Option rebuild in Grey.

### 5.1 The build log - one text for the three (decision D14)

The log is the block's too. **It is planned whole before the first tool runs, it is written in order, and the
three outlooks print the same text**, so a build can be reviewed afterwards and one outlook's log set beside
another's.

- **Planned before compilation.** A plan already holds every step, so the block lays out the complete log from
  it at once: the head, then each step's line and its full command line, in the order of the plan. That text
  exists before anything runs; it is what the Build Plan page shows, and what `gride-plan.exe` prints. A build
  then only fills each step's place with what its tool said and how it ended.
- **Written synchronously.** Steps of one wave run side by side, but the log never interleaves them: a step's
  output is held until every step before it in the plan has been written, then written whole. The log on the
  screen and in the file is always the planned order, each step in one piece - whatever order the tools finished in.
- **The same in the three.** The block gives every line - head, step lines, command lines, each diagnostic as it
  was read, the closing count - and an outlook prints what it is given and adds nothing of its own. A program is
  written by its name, and the folder the tools were taken from is named once in the head, so that the text does
  not differ with where an outlook is installed. Two outlooks building the same project give logs that differ in
  one line: the one that names the outlook.
- **Kept for review.** The same text goes, line by line as it is released, to
  `obj\<Config>\<Target>\<project>.build.log` (Black has such a file today; Grey and White get it). Each build
  replaces the file; the one before is kept as `.build.previous.log`. Every outlook has Build > Show Build Log.
  The file always holds the command lines; the Output pane shows them when `build.showCommands` is on.

Its shape, for a project of two C files:

```
GRIDE build   c-bank   Debug   x86_64-windows   12-11-2026 14:05:11   Black 1.5.0
tools         compile 1.0, toolchain.json as shipped; from C:\...\GRIDE\Black
files         Sources: *.c -> main.c, account.c
[1/4] compile   main.c       c90 1.3       chosen: by extension (.c is C; the project says auto)
      c90.exe -S main.c -o obj\Debug\Win\main.s -arch x86_64-windows -O0 -g -D_DEBUG=1 -masm=masm
[2/4] compile   account.c    c90 1.3       chosen: by extension (.c is C; the project says auto)
      c90.exe -S account.c -o obj\Debug\Win\account.s -arch x86_64-windows -O0 -g -D_DEBUG=1 -masm=masm
      account.c(14): warning: 'rate' is read here and nothing has been assigned to it
[3/4] assemble  main.s, account.s          masm 1.0
      masm.exe -t x64 obj\Debug\Win\main.s -o obj\Debug\Win\main.obj
      masm.exe -t x64 obj\Debug\Win\account.s -o obj\Debug\Win\account.obj
[4/4] link      c-bank.exe   link 1.0      runtime: grt 1.0 (Debug)
      link.exe /nologo /subsystem:console /out:out\Debug\Win\c-bank.exe ... /DEBUG
built         out\Debug\Win\c-bank.exe   0 errors, 1 warning   0.9 s
```

The `files` line says what each pattern of the build's groups gave (section 3.4); a file that was not there at
the last build is marked `(new)`, and a pattern that matched nothing is named.

A step that was up to date says so in its place (`[1/4] compile main.c - up to date`), so the log always has
every planned step. A failed step ends the log with the steps that were not run named as such.

### 5.2 The dry run - the build as a script, and nothing run (decision D17)

A menu item that writes into the Output pane every command a full build would run - each compile, each assembly
and the link - and runs none of them. It is the user's requirement of 2026-11-13, and it is the block's: the three
outlooks show the same text.

- **Marked at both ends.** The first line is `DRY RUN ONLY - nothing was compiled, assembled or linked: these are
  the commands a full build would run`, and the last is `DRY RUN ONLY - end`, so that the text cannot be taken for
  a build's log.
- **Every command whole.** Where the build log names a program by its name and a file from the project's folder, the
  dry run writes the command as it would be started: the program by its path, every argument, quoted as Windows
  wants them. With the folder the commands run in, the variables a command is given (`set LIB=...` above it) and
  the text of any file GRIDE would write for a tool (the C6747's `ti-link.cmd`, line by line), it is the script of
  the build.
- **The whole build, whatever is up to date.** A plan holds every step; which of them a build would skip is not the
  dry run's matter.
- **With the reasons.** Each step's line is the log's own - `[1/4] compile main.c c90 chosen: by extension ...` -
  so the dry run also answers why a tool was chosen, and a problem the plan found (a tool that is nowhere, a named
  file that is gone) is in its head.
- **Nothing is touched.** No tool is started, no folder is made, no file is written, the last build's log and
  stamps stay, and the Error List is left as it was.

```
DRY RUN ONLY - nothing was compiled, assembled or linked: these are the commands a full build would run
GRIDE build   c-bank   Debug   x86_64-windows   13-11-2026 11:20:00   Black 1.5.0
tools         compile 1.0, toolchain.json as shipped; from C:\Program Files\GRIDE\Black
project       D:\work\c-bank\c-bank.pro
folder        D:\work\c-bank   (every command runs here)
[1/4] compile   main.c       c90       chosen: by extension (.c is C; the project says auto)
      "C:\Program Files\GRIDE\Black\c90.exe" -S D:\work\c-bank\main.c -o D:\work\c-bank\obj\Debug\Win\main.s -arch x86_64-windows -g -D_DEBUG=1 -masm=masm
[2/4] compile   account.c    c90       chosen: by extension (.c is C; the project says auto)
      "C:\Program Files\GRIDE\Black\c90.exe" -S D:\work\c-bank\account.c -o ...
[3/4] assemble  main.s, account.s  masm      the target's assembler
      "C:\Program Files\GRIDE\Black\masm.exe" -t x64 D:\work\c-bank\obj\Debug\Win\main.s -o D:\work\c-bank\obj\Debug\Win\main.obj
      "C:\Program Files\GRIDE\Black\masm.exe" -t x64 D:\work\c-bank\obj\Debug\Win\account.s -o ...
[4/4] link      c-bank.exe   link      the target's linker; runtime: GRIDE's runtime (Debug)
      "C:\Program Files\GRIDE\Black\link.exe" /nologo /subsystem:console /out:D:\work\c-bank\out\Debug\Win\c-bank.exe ... /DEBUG
would build   D:\work\c-bank\out\Debug\Win\c-bank.exe
DRY RUN ONLY - end
```

**Where it is.** In Black (step 3): Build > Dry Run *project*, and the same item in the menu a right-click on a
project in the Workspace Explorer opens; a project's dependencies come first, each with its own dry run. Build >
Dry Run Workspace, also in the workspace's right-click menu, takes every project. Grey and
White get the item when they take the block (steps 4 and 5). From a command line: `gride-plan <project.pro> -dry`.
The Build Plan page of section 6 is the same text kept in a pane of its own; the dry run is its menu form and
comes first.

**CCS and Visual Studio sources are part of it** (the user's, the same day: "since GRIDE could also read the CCS's
workspace and the Visual Studio 2022 solution, the commands issued from that source must also be part of this Dry
Run"). The block does not plan those projects until step 8, so until then Black's own planner writes their dry
run: it goes through the whole build with a mark set, and wherever it would start a tool, clean a folder or write a
file it writes the command, or the file, instead. The text has the same first and last line, the same head with one
line more - `planner  Black's own: a CCS project is read from its own files, which compile.dll does not plan yet` -
then Black's lines for each step (`[c90]  main.c  ->  obj\main.s`) with the command written whole beneath, the
variables a command is given (`set INCLUDE=...` for cl, once) and `through cmd.exe, after: call <script>` where a
setup script is named. The same holds for a file built without a project and for a target whose toolchain page is
changed. **Build > Dry Run Workspace** does it for every project of a workspace - of a CCS workspace, of a Visual
Studio solution - in build order. When step 8 moves those readers into the block, their dry run becomes the
block's, and the `planner` line goes.

## 6. The pages - what a user sees

The same pages, with the same names and the same fields, in the three outlooks; each outlook draws them in its
own style and reaches them from its own menus. The fields come from the block (it lists the `build` keys, the
Compiler Options table and the tools), so a page cannot fall behind the model.

| page | holds | Black today | Grey today | White today |
|---|---|---|---|---|
| **GRIDE Paths** | the `build` keys of 3.2 | Tools > Options > Projects > GRIDE Paths, and part of Environment > General | Tools > Options > GRIDE Paths, and part of General | eight separate items in the Tools menu |
| **Compiler Options** (installation defaults) | `build.options`, from the table in `toolchain.json` | Tools > Options > Projects > Compiler Options | Tools > Options > Compiler Options | Tools > Compiler options... with no project open |
| **Tools** | every tool of `toolchain.json`: where it was found, its version, its templates; a change is written to the user's `toolchain.json`; "Restore" removes the change | Toolchains > four per-target pages (10 templates each) + Definitions | Toolchains + Definitions | three items in the Tools menu: the assembler and the linker for Windows, the linker for the C6747 |
| **Project Properties** | name, compiler, target, emulate only, arguments, include folders, libraries, per-configuration folders and flags | Project > Properties (Configuration tab) | Project > Properties | Project > Include Paths..., Libraries..., Build > Command-line arguments |
| **Project Compiler Options** | `.pro` `options`, per configuration | a tab of Project > Properties | Project > Compiler Options | Tools > Compiler options... with a project open |
| **Build Plan** (new, read-only) | the planned build log of 5.1 for the active project, configuration and target - every step, its full command line and why its tool was chosen, problems first - with each step's folder and environment on request; Build > Show Build Log opens the last build's log beside it | Tools > Toolchain Check (tools only); Run / Emulate / Debug templates that are shown but not what runs | none | Help > Environment; a command preview that omits the link it really does |

Consequences worth seeing before deciding (decision D7):

- **White gains two dialogs** (GRIDE Paths, Tools) and loses eleven menu items to them. "The Simple" stays
  simple in use - the defaults need no visit to either - but its Tools menu changes shape.
- **Black loses its four per-target toolchain pages**; what they edited is in the Tools page, per tool.
- **What is shown is what runs.** The Build Plan page is the block's own plan, so the three display-only
  templates of Black, Grey's ignored debugger entry and White's shortened preview all end.
- Help > About (the box the three share since today) could take its tool lookup from the block too.

## 7. The block

Form, as decided (D1, D8, D13):

- **A native DLL, `compile.dll`**, built once by `common\compile\compile.vcxproj` (toolset v143, `/MD`, no `/clr`)
  and copied beside each `GRIDE.exe`; Setup installs it with every outlook. Native, because Black and Grey are
  C++/CLI and can call native code directly, while White's core is native and cannot call managed code. A project
  reference in each outlook's solution and in `GRIDE3.sln`. It has its own number and line, as every component
  since Setup 1.6.3 - `compile 1.0 - plans GRIDE's build commands` - and Help > About lists it.
- **It exports its C++ classes** (`Workspace` and the functions below, marked `GRIDE_COMPILE_API`), and the
  outlooks call them directly. What that asks for, and how it is kept safe:
  - the DLL and its callers are built together, by the same compiler and against the same runtime DLLs (`/MD`),
    which `GRIDE3.sln` and Setup already guarantee; they are never mixed from two releases;
  - an outlook checks the DLL's number when it loads it and refuses, with a message, one that is not the number
    it was built with;
  - only `Workspace` and the free functions are exported, and `Workspace` keeps its data behind a pointer; `Step`,
    `Plan` and the other records are plain structures defined in the header, so no template class is exported;
  - a script cannot call exported classes, so tests and scripts go through `gride-plan.exe`, the block's
    command-line front end, which is the first user of the DLL.
- **Its header is `gride_compile.h`**, not `compile.h`: White's own runner is `src\compile.h`, and the two must not
  be mistaken for each other.
- A DLL keeps White's start-up rule out of the way: a native global with a destructor harms a mixed-mode `.exe`
  before `main`, not a separate native DLL. The block still avoids them.
- **C++11**, the level White's core uses in fact (its project says C++14; nothing in it needs more than C++11).
  White's rules hold for the block: no `<iostream>` (its globals corrupt the mixed-mode start-up), no global
  object with a destructor, no exception across the API, no process-wide state - in particular no `_putenv_s`:
  a step carries its own environment.
- **No operating-system call in the planner.** File times, environment variables, the registry and the folder of
  `GRIDE.exe` reach it through two small interfaces the outlook supplies, so it is testable anywhere.
- **Starting code**: White's native `json.cpp` (385 lines) and `ccs\*.cpp` (1,429 lines) are the only native
  readers in the tree and move in; Black's `ToolchainDefinitions.h` is the model for the `toolchain.json` reader.

The interface (the study's, with three additions marked +):

```cpp
// gride_compile.h - GRIDE_COMPILE_API is __declspec(dllexport) in the DLL and __declspec(dllimport) in a caller
namespace gride { namespace compile {

enum Action { Build, Rebuild, Clean, CompileFile, Run, Emulate, Simulate, Verify, Debug, Convert };

struct Request { std::string project, configuration, target; Action action;
                 std::string file; std::vector<std::string> arguments; };

struct Step {
    std::string kind, tool, program;          // compile | assemble | link | run ...; toolchain.json's id; the path found
    std::vector<std::string> argv;            // one element per argument, unquoted
    std::string directory;
    std::vector<std::pair<std::string, std::string> > environment;   // this step's own
    std::vector<std::string> inputs, outputs;
    std::string stamp, encoding, diagnostics, shown;
    std::string why;                          // + which rule of section 4 chose this tool, in words
    int wave;                                 // steps of one wave may run together
};

struct Plan { std::vector<Step> steps; std::vector<std::pair<std::string, std::string> > files;
              std::vector<Problem> problems; bool asksNative; };

class GRIDE_COMPILE_API Workspace {             // exported; its data is behind one pointer
public:
    static Workspace load(const Paths &, FileSystem &, Environment &);
    Plan plan(const Request &) const;
    std::vector<Setting> settings() const;            // + the build keys, their values and where each came from
    std::vector<OptionRow> compilerOptions() const;   // + the Compiler Options table, for the pages
    std::vector<ToolState> tools() const;             // + every tool: found where, or every place looked in
    Problems change(const Change &);                  // + one edit from a page, written to the right file
    Workspace(const Workspace &);  Workspace &operator=(const Workspace &);  ~Workspace();
private:
    struct Data;  Data *data_;
};

// + The build log (5.1): planned whole from a plan, then filled in the plan's order whatever order the steps end in.
class GRIDE_COMPILE_API BuildLog {
public:
    BuildLog(const Workspace &, const Request &, const Plan &, const std::string &outlook);
    std::string planned() const;                     // the complete log before anything runs
    std::vector<std::string> begin();                // the head; opens the log file
    std::vector<std::string> finished(int step, int exitCode, const std::string &output, double seconds);
                                                     // a step ended: the lines now due, in order - none while an
                                                     // earlier step is still running, several once it has ended
    std::vector<std::string> end();                  // the closing lines; closes the file
    std::string file() const;                        // obj\<Config>\<Target>\<project>.build.log
    int errors() const;  int warnings() const;       // as read by readDiagnostics, so the three count alike
    ~BuildLog();
private:
    BuildLog(const BuildLog &);  BuildLog &operator=(const BuildLog &);
    struct Data;  Data *data_;
};

GRIDE_COMPILE_API const char *version();          // + "compile 1.0 - plans GRIDE's build commands": an outlook checks it
GRIDE_COMPILE_API std::string windowsCommandLine(const std::string &program, const std::vector<std::string> &argv);
GRIDE_COMPILE_API std::vector<Diagnostic> readDiagnostics(const std::string &dialect, const std::string &line);

}}
```

**As built in step 2** the interface is `common\compile\gride_compile.h`, and it is the listing above with these
differences, each small:

- `Paths` (the two `toolchain.json`, `settings.json`, the program's folder, the outlook's name) and the two
  interfaces `FileSystem` and `Environment` are spelled out; the DLL offers this machine's as
  `systemFileSystem()`, `systemEnvironment()` and `defaultPaths()`, so three outlooks do not write them three times.
- `Request` also carries `native` (the user's yes to the native-tools question) and the workspace's folder.
- `Plan` also carries the program, the two folders, the folders a Clean or Rebuild empties, what each pattern of the
  build's groups gave (`listed`, for the log's `files` line) and the notes of what the block decided.
- `BuildLog` is given the time as text by its caller, since the block reads no clock, and has `upToDate(step)` for a
  step that did not need to run. Since step 3 it also has `dryRun()` (5.2), `showCommands(false)` - the lines handed
  back for the screen leave the command lines out, as `build.showCommands` asks, while the kept text has them and a
  step that fails shows its own - and `end(elapsed)`, the build's seconds by the caller's clock.
- `Workspace::files(project)` gives a project's groups entry by entry, for the explorers.
- `Stamps` answers which steps need to run (section 5, "Up to date"): a step's stamp against the one of its last
  run that succeeded, kept in `obj\<Config>\<Target>\<project>.stamps`, and what it reads against what it writes.
  The three outlooks ask it, so they rebuild alike; `gride-plan -build` is its first user.
- `settings()`, `compilerOptions()`, `tools()`, `change()` and `readDiagnostics()` are not there yet: they come with
  the steps that use them (6 and 7), so that nothing is exported that nothing calls.
- The two classes' data records are named `WorkspaceData` and `BuildLogData` outside the classes, because cpp11
  does not take a nested record defined outside its class - found by D9's test.

Rules the block keeps:

- **argv is the plan.** A template is cut into words by Windows' own rules and its placeholders are filled word
  by word; a list placeholder becomes as many words as it has entries; a path is one word whatever it holds.
  The command line is written from argv once, in one function. That ends the eleven quoting functions, the
  double-space collapse inside quoted paths (Black) and the split on escaped quotes (White).
- **One source per fact**: defines from the Compiler Options; folders from one rule; names from `toolchain.json`.
- **A response file** where a line would pass 30,000 characters.
- **Reading and writing files is the block's** for `toolchain.json`, the `build` object and the `.pro`; an
  outlook never writes those keys itself, so the three cannot write them differently.

Size: about 5,000-6,000 lines for the block, against about 22,000 of reading and planning in the three today, of
which the planners and readers go and the runners and dialogs stay.

## 8. How we know it is right

1. **A census before anything changes**: every project of `examples\projects` (29 `.pro`, the 3 CCS samples), in
   Debug and Release, for every target each outlook offers, built by each outlook as it is now; every command
   line kept, with paths made relative. This is step 1 and needs no decision.
2. **`gride-plan.exe`**, the block's command-line front end, prints the plan for the same inputs. Its output is
   compared with the census. Each difference must be one of: a defect of section 10 being fixed (listed), a
   choice of section 11 (listed against it), or a stop.
3. **The plans as files in the repository**: once agreed, the plan of each example is kept as a text file, and a
   test fails when a plan changes without the file changing - the way the conformance suite's baseline works.
4. **Unit tests** for the parts with rules: word-splitting and quoting, template filling, the merge of the two
   `toolchain.json`, the settings' move, object naming, stamps.
5. **The builds themselves**: after each outlook takes the block, the examples build and run in it (the 156-run
   set of 2026-11-11 for the tools; the three outlooks driven as in today's About checks), and a person builds
   and runs one project by hand in each outlook before a Setup is made.
6. **The logs set side by side**: the same project built in Black, Grey and White gives three `.build.log` files
   that differ in the one line naming the outlook, and each equals the planned log of `gride-plan.exe` with its
   steps' results filled in. This is checked for every example when an outlook takes the block.

## 9. Order of work

Each step is a branch and ends in something that can be released or dropped; none is started before the one
before it is agreed done.

| step | what | a user sees | done when |
|---|---|---|---|
| 0 (done) | one shipped `toolchain.json` (`common\toolchain.json`) | Grey's Debug builds link the Debug runtime | the two copies are gone; the examples build as before in Black and Grey |
| 1 (done) | the census of today's commands | nothing | every example's commands recorded for the three |
| 2 (done) | the block, `compile.dll`: readers, file patterns (3.4), lookup, Compiler Options, the build plan for the four targets, `gride-plan.exe`, tests | nothing | its plans match Black's census apart from listed fixes; a test rewrites each example with patterns and gets the same files as its name list gave |
| 3 (done) | Black takes it for Build, Rebuild, Clean, Compile File; Setup installs `compile.dll` and About lists it | Black's defects of section 10 gone | the census; the examples in Black |
| 4 (done) | Grey takes it | the same folders as Black; options rebuild | the same |
| 5 | White takes it; from here the three read patterns, so new projects are written with them, old lists are rewritten when nothing is lost, and the examples are converted | White's builds become incremental; its program moves to `out\...`; a `.pro` no longer lists every file | the same; White's examples run; the converted examples build in the three |
| 6 | the `build` vocabulary and the common pages (GRIDE Paths, Tools, Build Plan) in the three | a setting made in one outlook holds in the others | each page shows the same values in the three |
| 7 | run, emulate, simulate, debug and convert commands; the diagnostics reader | White shows warnings and linker errors | the three start the same programs with the same arguments |
| 8 | the CCS and Visual Studio readers | - | the CCS samples plan alike in the three |

Steps 0 and 1 change almost nothing a user sees and are worth doing whatever is decided about the rest.
A rough measure, not a promise: steps 0-1 a day; step 2 the largest; each of 3-5 smaller than 2; 6 about the size
of one of them.

## 10. What was checked, and what differs from the study

Four independent readings of the code, one per outlook and one of the formats, at `f698bc2`. The study's account
holds; these are the corrections and additions.

| | finding |
|---|---|
| G1 | **The two `toolchain.json` differ in four places, not three**, and one is deliberate: Grey's `cpp11.installIncludeDirs` is `[]` since 2026-11-11 (cpp11 must not be handed its own headers with `-I`; it warned). Black's still lists four folders. The one file takes Grey's value here and Black's for the Debug runtime (`grt.files.Debug`, the ten aliases) and the debugger's step filter. Copying Black's file whole would bring the warnings back. |
| G2 | The step filter that differs is **gride-dbg's** entry, not cdb's. |
| G3 | Black's **Run** template is not display-only: it runs when "run in IDE" is off. Emulate and Debug are display-only, as the study says. |
| G4 | White sets the build's environment process-wide, but **before** the parallel compiles start; no case was found where it changes under them. It is still state the block must not have. |
| G5 | White's Windows build: the compiler chooses "masm.exe beside it, else clang" itself when the settings name no assembler. With `cl` it compiles and links in one command. |
| G6 | White parses the first error only; a failed link and every asm6x or lnk6x message are not parsed at all. |
| G7 | Grey's debugger takes the *program* from the user's entry and ignores its arguments, environment and setup script. |
| G8 | Grey resets every `Tools[]` entry not marked `custom` from `toolchain.json` on each load: the list is already half a cache of the file. |
| G9 | Documentation that contradicts the code or itself: "identical byte for byte" (TOOLCHAIN-FORMAT.md:27, Settings_Files.html:30); a Debug build "through clang and link.exe /DEBUG" in three places against "needs no clang" in a fourth; `ownCodeView`, the `ownDebugger` role and a library's `crt` field undocumented; White "reads no toolchain.json" beside "as White reads them". These are corrected with step 0. |
| G10 | The up-to-date check counts a project's header *files*, not the headers a source includes, in Black and Grey alike. The design keeps that (section 5); tracking real includes is a later improvement. |

**What the census added** (step 1: 342 builds recorded, 114 per outlook; `common\compile\census\baseline`). These
are what the three do today, read from the recorded commands, and each is the block's to settle:

| | finding |
|---|---|
| C1 | **Grey compiles a file outside the build's groups**: `compilerpp`'s `sample.cpp` (group `Input`) is compiled with the 16 sources, fails, and the link is never reached - in all four of its builds. Black and White compile the 16. |
| C2 | **Only Black links the Debug runtime in a Debug build** (27 of 28 for Windows). Grey linked the Release one until step 0; White still does. |
| C3 | **Black links Microsoft's C runtime, not GRIDE's, for `mathx-windows`** (`libcmtd.lib`); Grey and White link GRIDE's with the same `mathx.lib`. |
| C4 | **Black adds `-I<project>\inc` and `-DDEBUG` / `-DNDEBUG`** to 244 of its 306 compiles, whether or not the project has an `inc` folder; these come from its default configuration. Grey gives `-I<install>\include\c`, `\include\win` and `\lib`; White `-I<install>/lib` only. |
| C5 | **White alone passes `/stack:8388608`**, on its 18 C++ links and on no C or Shalimar link. |
| C6 | **White compiles and links from other folders**: a Windows build runs `c90 -c` for all files at once in `%TEMP%\gride-parts-<pid>` with no `-arch` and no `-o`, the compiler starts masm itself, and the link runs from the install folder. |
| C7 | **The environment differs**: Black and Grey end INCLUDE and LIB in a literal `%INCLUDE%` / `%LIB%` and set an empty `CPP11_DECLINES`; White sets `C90_AS` / `C90_LD` to masm and link on C6747 builds too. |
| C8 | **A CCS project is built in different places**: Black in `<project>\.gride\`, Grey and White in `%TEMP%\gride-ccs-<Name>`. |
| C9 | **Eight Debug links for C6747 fail in every outlook that tries them**: `ccs\Sample` and `ccs\SampleExt` (`.text` does not fit in SHRAM) in the three, `compilerpp` (a relocation out of reach) in Black and White. Not a planning matter; listed so the comparison in step 2 expects them. |

**What step 2 showed** (the block's plans set against Black's census, and built):

| | result |
|---|---|
| P1 | **Black's 108 recorded builds of `.pro` projects, each compared with the block's plan as a set of commands: 57 the same, 51 the same after the two fixes below, none different.** The 52 generated `ti-link.cmd` files are the same text at the same place. (`compile-tests census`; the 6 builds of CCS projects are step 8's.) |
| P2 | **Fix: `-I<project>\inc` is no longer given** (244 compiles). Black invents a configuration for a project that stores none, with an include folder `inc` whether or not there is one (finding C4). The block invents nothing: a configuration is what the `.pro` holds. A project that stores `includeDirs: ["inc"]` keeps it. |
| P3 | **Fix: `-DDEBUG` / `-DNDEBUG` are no longer given** (122 each), from the same invented configuration: the define arrives once, from the Compiler Options (`-D_DEBUG=1`), as D6 decided. A project that stores `defines` keeps them. |
| P4 | **The environment is smaller** (section 5): the recorded steps carried `INCLUDE`, `LIB`, six `*_AS` / `*_LD` and an empty `CPP11_DECLINES`; the block's carry `LIB` on the link and nothing else for these projects. |
| P5 | **Visual Studio is found without `vswhere.exe`**: the block starts no process, and the default roots of `toolchain.json` gave the same installation, compiler version and SDK version as Black found. A Visual Studio outside those roots is named by `build.vcvars`, as now. |
| P6 | **A Debug link flag where Black gave none**: `linkFlags.Debug` is the linker's for every Debug link; Black added `/DEBUG` only when a compiler of the build wrote its own CodeView, which every example's does. A Debug project of assembly files alone would now get `/DEBUG`. |
| P7 | **Written as patterns, the 29 examples' groups give the files they gave**: 172 entries become 58, no project keeps a whole name list, and in 8 the order of a group's files changes from the order written to the order by name (`compile-tests patterns`). |
| P10 | **Built from the plans, the examples are the programs Black builds.** `gride-plan -build` ran the block's steps for all 108 builds: 107 built, and each of the 107 programs, run on Windows or on the C6747 simulator, prints what Black's program of the same build prints. The one that does not build is the one that does not build in Black either (`compilerpp`, Debug, C6747: a relocation out of reach, finding C9). **The same again from projects rewritten with patterns**: 107 built, 107 the same output. (`tests\run-examples.ps1`; the simulator's count of cycles and the folder a program prints are left out of the comparison.) So the smaller environment of P4 and the two fixes of P2 and P3 change no example's behaviour. |
| P11 | **Black and Grey do not mind the new fields**: with the extended `toolchain.json`, 24 builds of three examples recorded again in the two outlooks give the commands recorded after step 0. |
| P9 | **The order of a Shalimar project's files is part of the project** - found by building the examples from patterns: shalimar compiles a program's files in one run and the first is the program's own, so `*.shl` alone, which gives `numbers.shl` before `primes.shl`, built another program (it ran `numbers.shl`'s `main`). The rule of 3.4 was completed for it: a language compiled as one run keeps its first file by name in front of the pattern - `"primes.shl", "*.shl"`. Which languages those are is read from `toolchain.json` (a compile that names `$(Sources)`), not written in the block. Three examples are written so; C and C++ projects need no name. |
| P8 | **cpp11 compiles the block** (D9): the nine portable sources, after three constructs it does not take were written another way - a pair holding a type not yet complete (White's JSON class), a nested record defined outside its class, a function named `hash`. `host.cpp`, the one file that includes `windows.h`, is the machine's own compiler's. |

**What step 3 showed** (Black building through the block, 2026-11-13):

| | result |
|---|---|
| B1 | **Black runs the block's plan.** All 114 builds of the census recorded again from the new Black: in the 108 of `.pro` projects the commands it started are the block's plan, command for command (`compile-tests census` on the new recording: 108 the same, none different), and the 52 `ti-link.cmd` are the same text. The same three builds fail as before (finding C9). |
| B2 | **CCS projects are untouched**: the 6 builds of the CCS samples, which Black's own planner still plans, start the commands of the baseline. |
| B3 | **The environment as recorded**: the steps of a `.pro` build carry `LIB` on a Windows link and nothing else; the `*_AS` / `*_LD` pairs and the empty `CPP11_DECLINES` are seen only on the CCS builds, which the old planner runs. |
| B4 | **Nothing is invented at the source**: Black's default configuration no longer begins with the include folder `inc` and the define `DEBUG` / `NDEBUG` (finding C4), so its Properties page and its own planner agree with the block. A `.pro` that stores them keeps them. |
| B5 | **Up to date, in the program**: Build twice in one working copy - the second starts no tool and every step says `up to date`; after one source is touched, three tools start: its compile, its assembly and the link. |
| B6 | **The dry run, in the program** (5.2): Build > Dry Run on a `.pro` project, on a CCS project and Build > Dry Run Workspace on a Visual Studio solution each wrote the build's script into the Output pane between the two `DRY RUN ONLY` lines, and the recorder saw no tool started. |
| B7 | **The Output pane and the log**: with Black's "show command lines" off the pane shows each step's line and what its tool wrote, and `obj\<Config>\<Target>\<project>.build.log` holds every command line; Build > Show Build Log opens it. |
| B8 | **What Black still plans itself**, and says so in the Output pane: CCS and Visual Studio projects (step 8), Assemble Only and Link Only, a file built without a project, and a target whose page under Tools > Options > Toolchains is changed from its defaults - the block reads no template from `settings.json` (D5), so a changed page is honoured by the planner that reads it rather than dropped. `GRIDE_OLD_PLANNER=1` in the environment sends every build to the old planner, for comparing the two. |
| B9 | **The DLL is not a hard dependency of starting**: `compile.dll` is delay-loaded; Black checks that it is beside `GRIDE.exe` and says the line it was built with before anything of it is used, and builds with its own planner, saying why, when it is not. |
| B10 | **About lists it**: `compile 1.0 - plans GRIDE's build commands`, under Other tools, in the outlook that has taken the block. |

**What step 4 showed** (Grey building through the block, 2026-11-13):

| | result |
|---|---|
| G1 | **Grey runs the block's plan.** All 114 builds of the census recorded again from the new Grey: in the 108 of `.pro` projects the commands it started are the block's plan, command for command, and so the commands Black starts; the 6 of CCS projects, which Grey's own planner still plans, start the commands recorded after step 0. The same three C6747 Debug links fail as in every outlook. |
| G2 | **Finding C1 is gone**: `compilerpp` reaches its link in Grey - a file outside the build's groups is no longer compiled. With it go Grey's own `-I<install>\include\c`, `\include\win` and `\lib` (C4) and the second `-D_DEBUG` beside the Compiler Options' `-D_DEBUG=1`. |
| G3 | **One bridge.** `common\compile\clr\Block.h` and `Block.cpp` are what Black and Grey both compile to reach the DLL; Black, built with the moved bridge, starts the commands it started in step 3 (14 builds recorded again). |
| G4 | **Grey's folders follow the rule** (D3): `out\<Config>\<TargetFolder>` and `obj\<Config>\<TargetFolder>`, in the block and in Grey's own `OutputDirectory()` / `ObjectDirectory()`, so Run, Emulate and Debug look where the build writes. `build\$(Target)\$(Config)`, which Grey gave every configuration, reads as the rule. A configuration that names an output folder of its own keeps it, with `obj` under it. |
| G5 | **Grey's configurations are read as they are**: the block reads the `buildConfigurations` list - an entry's defines, flags, link flags and output folder - and a configuration has a name and a kind: Release when its name says so, Debug otherwise (Grey's rule). Writing them back as `configurations` (D6) waits until the block writes a `.pro` (step 6). |
| G6 | **Up to date and the dry run, in the program**: a second build starts no tool, a touched source three; Build > Dry Run on a `.pro` and on a CCS project wrote the script and started none. Grey's own planner writes the dry run of CCS and Visual Studio projects, as Black's does. |
| G7 | **What Grey still plans itself**, and says so: CCS and Visual Studio projects (step 8), and every build while a tool under Tools > Options > Toolchains is marked changed (`custom`) - the same reason as Black's B8. |

Open after step 3: a tool's version in its line of the log (Black has the numbers, from About's probe, and does not
hand them over yet); the move of a changed toolchain page into the user's `toolchain.json` (D5), which would let
B8's last case go.

The defects the study lists were confirmed where checked: in Black, an `emulateOnly` C6747 project rebuilt on
every build, `$(Optimise)` filled and unused, assembler and linker output read as OEM, a `.s` on Linux or macOS
failing on `as`; in Grey, the stamp that misses the Compiler Options, `$(AsmFlags)` in no template, only the
first linker command file, `.c++` and `.C`, a Linux or macOS target lost on save; in White, no `-arch` on host
compiles, one program file for Debug and Release, leaf-name object clashes, a CCS project's sources taken as the
union of its configurations.

## 11. The decisions, as taken

Discussed with the user on 2026-11-12. "As recommended" means draft 1's recommendation was chosen; the two marked
**changed** are the user's own choice against it, and the document follows them.

| | decision | decided | what it means |
|---|---|---|---|
| D1 | Build the block, or only steps 0 and 1 | **changed: build it, as a DLL** - "if by library you mean a dll, then it's approved" | The block is `compile.dll`, not a static library. Steps 0 and 1 still come first and the census is still the gate for step 2. |
| D2 | White's Windows pipeline | as recommended: one unified pipeline, inside the DLL | `-S`, then the assembler, then the linker in the three; White leaves `-c`. |
| D3 | Folders | as recommended: Black's rule in all three | `out\<Config>\<Target>` and `obj\<Config>\<Target>`; Grey's and White's output moves. |
| D4 | The settings' names | as recommended: one `build` section | White's short names; old keys read once and still written for one release. |
| D5 | Where a tool is changed | as recommended: the user's `toolchain.json` only | Black's per-target templates and Grey's `Tools[]` are moved there once. |
| D6 | Configurations in `.pro` | as recommended: Black's form for all | `configurations` keyed `"Debug\|Windows"`; Grey's key read and rewritten; defines from the Compiler Options only. |
| D7 | The pages | as recommended: the same six pages in all | White gains GRIDE Paths and Tools dialogs; Black's four per-target pages become one. |
| D8 | Name and place | **the user's: `compile.dll`** | Sources in `common\compile\`, header `gride_compile.h`, namespace `gride::compile`, front end `gride-plan.exe`. |
| D9 | Language level | as recommended: C++11 | cpp11 itself compiles the block's sources as a test. |
| D10 | The defects of section 10 | as recommended: through the DLL | Only Grey's Debug runtime is fixed at once, in step 0. |
| D11 | Extensions | as recommended: one list, widened | `.c`; `.cpp .cc .cxx .c++`; `.shl .shm`; `.s .asm`; `.C` as `.c` on Windows. |
| D12 | This document's home | not asked; draft 1's proposal stands unless you say otherwise | Here in `common\` while the work goes on; `docs\` (and so the Setup) when it is done. |
| D13 | How the outlooks call the DLL | **changed: exported C++ classes** (recommended was plain C functions returning JSON) | Section 7: built together with its callers, a version check at load, data behind a pointer, scripts through `gride-plan.exe`. |
| D14 | The build log | **the user's requirement, added after the discussion**: planned whole before compilation, written in order, the same text in the three, kept for review | Section 5.1: the block lays out the log from the plan, releases each step's lines in the plan's order, writes `<project>.build.log`; the outlooks print what they are given. |

| D15 | Files in the `.pro` | **the user's requirement, 2026-11-12**: "`.cpp` or `.h`/`.hpp` anywhere, `*.cpp` or `*.hpp`/`.h` be listed; any subpath, `backend/*.cpp`"; no complete list of files in a project file | Section 3.4: a group's entry is a pattern, a name is a pattern for one file, resolved by the block at every plan; `exclude`; a fixed order. Two points asked and decided as recommended: each folder is listed and none is searched below itself (no `**`); an old name list is rewritten as a pattern on save when nothing is lost by it. |
| D16 | The project file's extension, and `.grws` | **the user's: `.pro` for the project; `.grws` kept for the workspace** - "if there are two files, a workspace file and a project file, only then we may retain `.grws`"; asked, and answered: two files, keep `.grws` (recommended was one file, a `.pro` listing projects) | Section 3.5: the `.pro` is the one common project format; the `.grws` stays Black's workspace file, unchanged, and is ratified as such. |

| D17 | A dry run | **the user's requirement, 2026-11-13**: "a 'Dry run' like menu option ... which would only log the script which would be run completely to make an exe, both compilation as well as linking ... on the top 'DRY RUN ONLY'" | Section 5.2: `BuildLog::dryRun()` in the block; Build > Dry Run and the project's right-click menu in Black; `gride-plan -dry`. Every command whole, marked at both ends, nothing run or written. |

What is still open, and not for now: the wording of each page (section 6) is settled when step 6 is reached, on a
picture of each, as the About box was.

## 12. Risks

- **White changes most**: new folders, explicit steps, incremental builds, two new dialogs. It is also the outlook
  with the most defects fixed by the change.
- **A user's edited templates** in Black and Grey must be carried into the user's `toolchain.json` exactly once;
  a template the move cannot express is kept and reported, not dropped.
- **A big change with no manual tester.** The census, the plan files and the staged order are there for this:
  every step is checked against commands recorded before it, and can be dropped without the next.
- **One working copy.** The steps are branches merged by git, never files copied in - the lesson of 2026-11-11.
- **A DLL that exports classes is tied to the compiler that built it.** A `compile.dll` from one release beside a
  `GRIDE.exe` from another would fail in ways hard to read. Hence the version check at load, one build for all
  (`GRIDE3.sln`), and a Setup check that the DLL beside each outlook is the one it was built with. Should a tool
  outside this build ever need the block, it uses `gride-plan.exe`, or a plain C layer is added then.
- **A pattern takes what is in the folder.** A scratch file, a copy named `main - Copy.cpp`, a second `main` left
  beside the first: with `*.cpp` they are in the build without anyone adding them. The build log's head lists
  what each pattern gave, a file new since the last build is marked there, and `exclude` is one click away.
- **The link order changes for a converted project** from the order written to the order by name. No example
  depends on it; a project that does keeps its names, or orders its entries.
- **Patterns before every outlook knows them.** Until an outlook takes the block (steps 3 to 5) it reads `*.cpp`
  as a file's name and finds none. So the examples are converted, and patterns written, only from the step at
  which the last of the three has taken the block (step 5); step 2 proves by a test that the lists come out equal.
  A project with patterns opened in an older Setup shows no files; the Setup that brings patterns says so in its notes.
- **A common name.** `compile.dll` is a name another program could carry; Windows loads the one beside `GRIDE.exe`
  first, and the block is never put on `PATH`.
