# The block - `compile.dll`

One piece of code that turns a project, its settings and `toolchain.json` into the commands of a build, for
GRIDE's three outlooks. It plans and never runs. The design is `common\Common_Compilation_Design.md`; this folder
is its step 2. **Black and Grey build through the block** (steps 3 and 4, 2026-11-13; their bridge is
`GRIDEd\GRIDE\src\Services\Block.cpp`). Grey takes it in step 4, White in 5.

| here | what |
|---|---|
| `gride_compile.h` | the interface: `Workspace`, `Plan`, `Step`, `BuildLog`, `Stamps`, the two interfaces a caller supplies |
| `src\` | the block's sources (C++11) |
| `compile.vcxproj` | builds `compile.dll` (native, `/MD`) |
| `plan\` | `gride-plan.exe`, the command-line front end and the DLL's first user |
| `tests\` | `compile-tests.exe` and `run-examples.ps1` |
| `census\` | step 1: what the three outlooks ran before the block existed - the ground truth the plans are set against |

## The sources

| file | holds |
|---|---|
| `json.*` | the JSON reader and writer (White's, moved in) |
| `text.*` | words, paths, command lines: cutting a template into words, filling it, writing a command line from argv |
| `definitions.*` | `toolchain.json` as records; the merge of the user's file over the shipped one |
| `project.*` | a `.pro`; a project's files by pattern (design 3.4); `settings.json`'s `build` keys with the three old vocabularies |
| `context.*` | where things are on the machine: tools, libraries, the installation, Visual Studio, TI's compiler |
| `plan.cpp` | the planner: the design's section 4, rule by rule |
| `log.cpp` | the build log of design 5.1 |
| `stamps.cpp` | which steps need to run: a step's stamp, what it writes and what it reads (design 5, "Up to date") |
| `workspace.cpp` | the exported class |
| `host.cpp` | this machine's files, environment and registry - the only file that calls the operating system |

The planner asks every question about the machine through the caller's `FileSystem` and `Environment`, so the
tests give it a machine that is not there. Nothing in the sources names a tool, a flag or a folder: those are
`toolchain.json`'s.

## Building

```
MSBuild.exe common\compile\plan\gride-plan.vcxproj   /p:Configuration=Release /p:Platform=x64
MSBuild.exe common\compile\tests\compile-tests.vcxproj /p:Configuration=Release /p:Platform=x64
```

Both go to `common\compile\x64\<Configuration>\`; the first builds `compile.dll` too. `compile` and `gride-plan` are in
`GRIDE3.sln` and in Black's `GRIDE.sln`, are built with Black, and are copied beside its `GRIDE.exe`.

## `gride-plan.exe`

```
gride-plan <project.pro> [-config Debug|Release] [-target <arch>] [-rebuild | -clean | -compile <file>]
           [-tools <folder>] [-settings <file> | -no-settings] [-outlook <name>] [-native]
           [-steps | -files | -build | -dry]
```

With nothing more it prints the build log as it is planned before anything runs. `-steps` prints every step for a
script. `-files` prints the project's groups with what each pattern gave. `-build` runs the steps one after another
and prints the log as it is released - the block plans, and this program, like an outlook, runs; a step that is
up to date is not run (`-rebuild` runs them all). `-dry` prints the dry run of the design's 5.2: `DRY RUN ONLY`,
then every command of a full build written whole, and nothing run. `-tools` is the
folder that holds `toolchain.json` and the tools (an outlook's `x64\Release`); the default is the program's own.

## Testing

```
compile-tests                                                       the unit tests
compile-tests census <census\baseline\Black> <examples\projects> <tools folder> [-v]
compile-tests patterns <examples\projects>
powershell -File tests\run-examples.ps1 -Tools <tools folder> [-Only c-bank,cpp-*] [-Patterns]
```

- **The unit tests** cover the parts with rules: cutting and quoting words, filling a template, paths, the merge of
  the two `toolchain.json`, the settings' three vocabularies, patterns, object naming, the Compiler Options, the
  compiler of a source, stored configurations, Clean, Compile File, the response file, a tool that is nowhere, and
  the build log's order.
- **`census`** sets every build Black recorded in step 1 against the block's plan for the same project, as sets of
  commands, and says for each: the same, the same after the listed fixes, or different.
- **`patterns`** writes every example's name lists as patterns and checks that each group gives the files it gave.
- **`run-examples.ps1`** builds the examples from the plans with `gride-plan -build`, runs the programs, and compares
  what each prints with what Black's program of the same build prints. `-Patterns` does it from projects that list
  no file by name.

The portable sources (all of `src\` but `host.cpp`) also compile with GRIDE's own cpp11, which the design asks of
them (D9):

```
cpp11 -S common\compile\src\plan.cpp -o plan.s -arch x86_64-windows -DGRIDE_COMPILE_STATIC
```

## What step 2 holds, and what it leaves

Planned: Build, Rebuild, Clean and Compile File of a `.pro`, for the four targets of `toolchain.json`.

Left to their steps of the design: `settings()`, `compilerOptions()`, `tools()` and `change()` for the pages (6);
run, emulate, simulate, debug, convert and the diagnostics reader (7) - until then the log counts errors and
warnings by the words; CCS and Visual Studio projects and Black's `.grws` (8).

Not done in step 2, and to be decided when Black takes the block: a tool's version in its line of the log (the
block starts no program, so the outlook, which already asks the tools for Help > About, has to hand the numbers
over); TI's own runtime standing in when RTS6x is not there; the Shalimar runtime assembled from its sources where
its library is not packed; steps of one wave run side by side (`gride-plan -build` runs them one after another).
