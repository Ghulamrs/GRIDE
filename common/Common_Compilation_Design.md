# Common compilation for the three outlooks - design document

Fable 5.2, 2026-11-12. **Draft 1, for discussion. Nothing described here is implemented.**

Written after `GRIDE common command block - study - Opus 5.5.md` (Documents folder), whose proposal this
document accepts in the main and turns into a design: what the one model is, how a project's tools are chosen,
where every setting lives, what each outlook's pages become, and in what order it is built. Section 11 lists the
decisions that are yours; nothing starts before they are taken.

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
5. **One piece of code that turns 1-4 into commands** - a native library the three link, which *plans* and never
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
                         gridecmd  (native library)      Workspace::load(...)   then   plan(Request)
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
by Setup for all three. Black's and Grey's copies go; White ships it for the first time. Its content is Black's,
with Grey's one deliberate difference kept (section 10, item G1).

Everything a build needs to know about a tool is in this file, and **every key of it is read**. The names that
are still in code in all three move into it: `/NODEFAULTLIB` and its list, `/DEBUG`, `/subsystem:console`,
`/stack:8388608`, `-mv6740 --abi=eabi`, `--rom_model`, the runtime's layer names, the folder names `obj` and
`shmrt`, the source and header extensions. New fields the design needs:

| field | on | meaning |
|---|---|---|
| `responseFile` | a tool | how the tool takes a file of arguments (`"@$(File)"`); used when a line passes 30,000 characters |
| `sourceExtensions`, `headerExtensions` | a compiler / a target | which files a compiler takes; replaces three lists in code |
| `folders` | top level | the output and object folder patterns (section 5) |
| `linkFlags.debug`, `linkFlags.always` | a linker | what is `/DEBUG`, `/subsystem:console /stack:...` in code today |

A user's changes go into the user's `toolchain.json` and nowhere else (decision D5). The merge rule stays as it
is documented: objects merge field by field, anything else replaces the shipped value.

### 3.2 One vocabulary in `settings.json`

A new object, **`"build"`**, holds every setting that affects what a build does. Its keys take White's short
names, because Grey's own page already labels them so. Editor, layout, recent files and key sets stay where each
outlook keeps them.

| `build.` key | meaning | Black today | Grey today | White today |
|---|---|---|---|---|
| `include`, `lib` | the installation's header and library folders | `environment.grideIncludeDir`, `grideLibDir` | `GrideIncludeDir`, `GrideLibDir` | `include`, `lib` |
| `includes`, `libraries` | include folders and libraries every project gets | `environment.sharedIncludes`, `sharedLibraries` | `SharedIncludes`, `SharedLibraries` | `includes`, `libraries` |
| `compiler` | the compiler when a project says `auto` or names none | `environment.defaultCompiler` | `DefaultCompiler` | `compiler` |
| `windowsRuntime` | GRIDE's runtime, Microsoft's, or automatic | `environment.windowsRuntime` | `WindowsRuntime` | `windowsRuntime` |
| `windowsDebugger`, `c6747Debugger` | which debugger | `environment.*` | `WindowsDebugger`, `C6747Debugger` | `windowsDebugger` |
| `vcvars`, `ti`, `tilib` | Visual Studio and TI's compiler, where used | `environment.vcvars`, `tiCompilerDir`, `tiLibDir` | `Vcvars`, `TiCompilerDir`, `TiLibDir` | `vcvars`, `ti`, `tilib` |
| `jobs` | files compiled at once | `environment.buildJobs` | `BuildJobs` | `buildJobs` |
| `askNative` | ask before falling back to Microsoft's or TI's tools | `environment.askNativeTools` | `AskNativeTools` | `askNative` |
| `options` | the Compiler Options' defaults for new projects | `compilerOptions` | `CompilerOptions` | `options` |
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

**Up to date**: each step has a stamp - its program's path, its argv and the environment it is given, hashed. A
step runs when its stamp changed or an input is newer than an output. Headers are the project's header files, as
today. This gives White incremental builds, and makes a changed Compiler Option rebuild in Grey.

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
| **Build Plan** (new, read-only) | the plan for the active project, configuration and target: every step's full command line, its folder, its environment, and why its tool was chosen; problems first | Tools > Toolchain Check (tools only); Run / Emulate / Debug templates that are shown but not what runs | none | Help > Environment; a command preview that omits the link it really does |

Consequences worth seeing before deciding (decision D7):

- **White gains two dialogs** (GRIDE Paths, Tools) and loses eleven menu items to them. "The Simple" stays
  simple in use - the defaults need no visit to either - but its Tools menu changes shape.
- **Black loses its four per-target toolchain pages**; what they edited is in the Tools page, per tool.
- **What is shown is what runs.** The Build Plan page is the block's own plan, so the three display-only
  templates of Black, Grey's ignored debugger entry and White's shortened preview all end.
- Help > About (the box the three share since today) could take its tool lookup from the block too.

## 7. The block

Form, as the study proposes and the code confirms as the only one possible:

- **A native static library**, `common\gridecmd\gridecmd.vcxproj`, toolset v143, `/MD`, no `/clr`. Black and Grey
  are C++/CLI and call native code directly; White's core is native and cannot call managed code. A project
  reference in each outlook's solution and in `GRIDE3.sln`.
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
namespace gride { namespace cmd {

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

class Workspace {
public:
    static Workspace load(const Paths &, FileSystem &, Environment &);
    Plan plan(const Request &) const;
    std::vector<Setting> settings() const;            // + the build keys, their values and where each came from
    std::vector<OptionRow> compilerOptions() const;   // + the Compiler Options table, for the pages
    std::vector<ToolState> tools() const;             // + every tool: found where, or every place looked in
    Problems change(const Change &);                  // + one edit from a page, written to the right file
};

std::string windowsCommandLine(const std::string &program, const std::vector<std::string> &argv);
std::vector<Diagnostic> readDiagnostics(const std::string &dialect, const std::string &line);

}}
```

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

## 9. Order of work

Each step is a branch and ends in something that can be released or dropped; none is started before the one
before it is agreed done.

| step | what | a user sees | done when |
|---|---|---|---|
| 0 | one shipped `toolchain.json` (`common\toolchain.json`) | Grey's Debug builds link the Debug runtime | the two copies are gone; the examples build as before in Black and Grey |
| 1 | the census of today's commands | nothing | every example's commands recorded for the three |
| 2 | the block: readers, lookup, Compiler Options, the build plan for the four targets, `gride-plan.exe`, tests | nothing | its plans match Black's census apart from listed fixes |
| 3 | Black takes it for Build, Rebuild, Clean, Compile File | Black's defects of section 10 gone | the census; the examples in Black |
| 4 | Grey takes it | the same folders as Black; options rebuild | the same |
| 5 | White takes it | White's builds become incremental; its program moves to `out\...` | the same; White's examples run |
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

The defects the study lists were confirmed where checked: in Black, an `emulateOnly` C6747 project rebuilt on
every build, `$(Optimise)` filled and unused, assembler and linker output read as OEM, a `.s` on Linux or macOS
failing on `as`; in Grey, the stamp that misses the Compiler Options, `$(AsmFlags)` in no template, only the
first linker command file, `.c++` and `.C`, a Linux or macOS target lost on save; in White, no `-arch` on host
compiles, one program file for Debug and Release, leaf-name object clashes, a CCS project's sources taken as the
union of its configurations.

## 11. Decisions for you

My recommendation is given with each; the alternative is what else could be done.

| | decision | recommended | alternative |
|---|---|---|---|
| D1 | Build the block, or only steps 0 and 1 | Steps 0 and 1 now; the block after you have seen the census | Stop after step 1 and fix each outlook's defects where they are |
| D2 | White's Windows pipeline | `-S`, masm, link, as Black and Grey | White keeps `-c`: the block then carries two pipelines |
| D3 | Folders | `out\<Config>\<Target>` and `obj\...` in the three | Grey keeps `build\...`, White its single `<name>.exe`: the block then carries a pattern per outlook |
| D4 | The settings' names | a `build` object with White's short names; old keys read once and kept for one release | each outlook keeps its names and the block maps them: no move, and the three vocabularies stay |
| D5 | Where a tool is changed | the user's `toolchain.json` only; Black's per-target templates and Grey's `Tools[]` are moved there once | keep them, read after the file: three places to look when a command surprises |
| D6 | Configurations in `.pro` | Black's `configurations`; Grey's `buildConfigurations` read and rewritten; defines from the Compiler Options only | a new neutral form, converting both |
| D7 | The pages | the same six pages in the three (section 6), White's Tools menu regrouped | the same data, each outlook keeping its present pages; only the Build Plan page added |
| D8 | Name and place | `common\gridecmd`, beside today's `common\AboutBox.h` | another name |
| D9 | Language level | C++11, and cpp11 itself compiles the block as a test | C++98 |
| D10 | The defects of section 10 | through the block, except Grey's Debug runtime (step 0) | fix each in its outlook now, and again in the block |
| D11 | Extensions | `.c++` C++, `.C` as `.c` on Windows, `.shm` Shalimar beside `.shl` | keep each outlook's present list |
| D12 | This document's home | here in `common\` while a draft; `docs\` (and so the Setup) once agreed | - |

## 12. Risks

- **White changes most**: new folders, explicit steps, incremental builds, two new dialogs. It is also the outlook
  with the most defects fixed by the change.
- **A user's edited templates** in Black and Grey must be carried into the user's `toolchain.json` exactly once;
  a template the move cannot express is kept and reported, not dropped.
- **A big change with no manual tester.** The census, the plan files and the staged order are there for this:
  every step is checked against commands recorded before it, and can be dropped without the next.
- **One working copy.** The steps are branches merged by git, never files copied in - the lesson of 2026-11-11.
